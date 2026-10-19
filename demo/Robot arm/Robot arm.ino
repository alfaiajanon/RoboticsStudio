// RoboticsStudio Robot arm firmware -- orange ball follower (closed-loop)
//
// Visual servoing with PID control and differential inverse kinematics.
//
// Every camera frame:
//   1. Vision      finds the orange blob: centroid and coverage (orange fraction).
//   2. Localizer   centroid -> ray, coverage -> range (bigger blob = closer);
//                  from the pose the arm had when the frame was exposed this
//                  gives the ball point in the robot frame.
//   3. BallFilter  alpha-beta filter on that point: position + velocity, and
//                  a prediction across the camera latency (Smith-predictor
//                  style: errors are measured against the arm's pose NOW).
//   4. Servoing    azimuth  (ball vs. arm heading about the base axis) -> PID -> yaw rate
//                  bearing  (ball vs. camera axis, in the arm plane)   -> PID -> bearing rate
//                  both PIDs feed forward the ball's own motion and use
//                  the relative rate as their D term.
//                  coverage vs. target -> PI -> stand-off setpoint (too much
//                  orange = too close -> the setpoint grows -> back away);
//                  analytic look-at IK at that stand-off -> posture reference.
//   5. DiffIK      shoulder/elbow rates with strict priority:
//                    1. bearing rate through the task Jacobian (fast, keeps the ball centred)
//                    2. glide towards the posture reference, inside the null
//                       space of task 1 (slow, sets the distance without
//                       disturbing the aim)
//                  damped, and weighted away from joint limits and the floor.
// Every tick, Arm integrates the joint rates under velocity and acceleration
// limits, so motion is always smooth: accelerate, cruise, settle.
//
// Assembly (Robot arm.rsproj), all MG996R servos, angles in degrees:
//   Yaw       base yaw   (axis +Z, positive = counter-clockwise seen from above)
//   Shoulder  shoulder   (positive = upper arm up)
//   Elbow     elbow      (positive = forearm bends down)
//   Camera    OV7670 camera on the forearm tip, looking along the forearm
// With no wrist, the camera's pitch is (shoulder - elbow).
//
// One loop, as on a real board: the arm is ticked every ~15 ms, and the
// camera is asked for a frame only once the next one is due
// (esp_camera_fb_get() waits for it, so asking early would stall the arm).
// A dropped frame costs one longer wait, during which the servos hold.
//
// Modules, top to bottom:
//   CONFIG, math helpers
//   Kinematics   forward kinematics, analytic look-at IK
//   Vision       orange colour segmentation
//   Localizer    detection -> ball point
//   BallFilter   ball position/velocity estimate
//   PID          PID with feed-forward and anti-windup
//   Servoing     ball estimate -> task rates
//   DiffIK       task rates -> joint rates
//   Arm          joint state, limits, pose history, servo I/O
//   Clock        simulated time per loop() tick
//   setup()/loop()

#include <Servo.h>
#include <math.h>
#include "esp_camera.h"
#include "robot_pins.h"

// ============================================================================
// CONFIG
// ============================================================================
namespace CONFIG {
    // Arm geometry, from the .rsdef connectors chained as in Robot arm.rsproj.
    // Planar coordinates (rho, z): rho = horizontal distance from the yaw axis.
    namespace geometry {
        const double yawAxisX = 0.01;                    // yaw axis is the vertical line x = 0.01, y = 0
        const double shoulder[2] = {0.0096, 0.0681};     // shoulder joint (rho, z)
        const double upperArm = 0.100;                   // shoulder -> elbow
        const double forearm = 0.1352;                   // elbow -> camera lens, along the forearm
        const double lensOffset = -0.0073;               // lens offset perpendicular to the forearm (up = +)
    }

    // Degrees. Commands are hard-clamped to these; DiffIK steers away from them.
    namespace limits {
        const double yaw[2] = {-88, 88};
        const double shoulder[2] = {-85, 85};
        const double elbow[2] = {-85, 85};
        const double minCameraZ = 0.035;                 // keep the lens off the floor (m)
    }

    // Degrees, the edit-mode pose of Robot arm.rsproj. Hobby servos have no
    // feedback wire, so the arm starts by commanding a known pose and then
    // tracks what it commanded.
    const double startPose[3] = {0, 81, 34};

    // OV7670 device: 160x120, fovy 45 deg, latency_ms 60, 15 fps
    namespace lens { const double fovyDeg = 45; const double latency = 0.06; const double framePeriodMs = 1000.0 / 15; }
    const double ballRadius = 0.015;                     // stage prop sp1, turns coverage into range

    namespace vision {
        const int stride = 2;                            // sample every 2nd pixel in x and y
        const double minHueDeg = 10, maxHueDeg = 50;
        const double minSaturation = 0.45;
        const int minValue = 60;                         // 0..255
        const int minSamples = 3;                        // fewer orange samples = not seen
    }

    namespace localizer {
        const double rangeSmoothing = 0.4;               // low-pass on the coverage-based range
        const double initialRange = 0.2;
    }

    namespace ballFilter {
        const double alpha = 0.5;                        // position correction per frame
        const double beta = 0.1;                         // velocity correction per frame
        const double maxSpeed = 1.0;                     // m/s, clamp on the velocity estimate
        const double predict = 0.10;                     // s ahead: camera latency + about half a frame
    }

    // PID per task:  u = ff*ballRate + kp*e + ki*int(e) + kd*(ballRate - ownRate)
    //   kp [1/s], ki [1/s^2], kd [-], ff 0..1, limit: output clamp.
    struct PidConfig { double kp, ki, kd, ff, limit; };
    namespace servoing {
        const PidConfig azimuth = {3.0, 0.3, 0.15, 0.8, 80 * M_PI / 180};   // rad/s
        const PidConfig bearing = {3.0, 0.3, 0.15, 0.8, 70 * M_PI / 180};   // rad/s
        // PI on the stand-off setpoint: error in m (from coverage), output m/s.
        const PidConfig distance = {2.0, 0.2, 0, 0, 0.10};
        const double coverageSmoothing = 0.4;            // low-pass on coverage
    }

    // The servos' +-90 deg range leaves a narrow window of reachable
    // stand-offs: for a ball on the floor 0.30 m out about 0.07-0.14 m,
    // 0.50 m out 0.26-0.34 m. Outside it, the nearest reachable one is used.
    namespace standoff {
        const double targetCoverage = 0.06;              // fraction of the image that should be orange (~0.11 m)
        const double deadband = 0.15;                    // +-15 % around the target: hold distance
        const double initial = 0.11;
        const double min = 0.05;
        const double max = 0.25;
        const double ikMax = 0.60;                       // the look-at may go this far to keep the ball in view
    }

    namespace diffIk {
        const double damping = 0.05;                     // DLS lambda for the bearing task (rad)
        const double secondaryDamping = 0.02;            // DLS lambda for the floor task (m)
        const double postureGain = 3.0;                  // 1/s: how fast the arm glides to the posture reference
        const double limitMargin = 15;                   // deg: joints get "heavier" this close to a limit
        const double floorGain = 4;                      // 1/s: how hard the lens is kept above minCameraZ
        const double floorMargin = 0.02;                 // m: floor guard starts this far above minCameraZ
    }

    namespace arm {
        const double maxVel[3] = {80, 70, 80};           // deg/s  yaw, shoulder, elbow
        const double maxAcc[3] = {300, 250, 300};        // deg/s^2
    }

    // Ball not seen:
    //   1..coastFrames             dropped frame? keep tracking the prediction
    //   ..searchAfterFrames        re-acquire: turn towards the edge it left by
    //   later                      full search sweep from the home posture
    namespace lost {
        const int coastFrames = 2;
        const double decay = 0.7;                        // per coasted frame, on the ball velocity
        const double reacquireRate = 60;                 // deg/s
        const int searchAfterFrames = 30;                // ~2 s at 15 fps
    }

    namespace search {
        const double yawRate = 35;                       // deg/s sweep
        const double span[2] = {-80, 80};
        const double homeGain = 2.0;                     // 1/s: shoulder/elbow pulled back to the home posture
        const double homeLookAt[3] = {0.30, 0.0, 0.015};
        const double homeDistance = 0.11;
    }

    const unsigned long loopMs = 15;
    const int logEvery = 15;                             // frames
}

const double DEG = M_PI / 180;
static double clampd(double v, double lo, double hi) { return fmin(hi, fmax(lo, v)); }
static bool inRange(double v, const double r[2]) { return v >= r[0] && v <= r[1]; }
static double wrapPi(double a) { return atan2(sin(a), cos(a)); }
static double signOf(double v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
static double hypot3(const double v[3]) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// ============================================================================
// Kinematics (degrees in, metres/radians out)
// ============================================================================
namespace Kinematics {
    namespace g = CONFIG::geometry;
    namespace lim = CONFIG::limits;

    struct Planar { double rho, z, pitch; };
    struct CameraPose { double pos[3], forward[3], right[3], up[3]; };
    struct PlanePoint { double azimuth, rho, z; };

    // Lens in the arm plane: {rho, z, pitch (rad)}.
    Planar planar(double sh, double el) {
        const double a = sh * DEG, b = (sh - el) * DEG;    // upper arm angle, forearm angle (= camera pitch)
        return {
            g::shoulder[0] + g::upperArm * cos(a) + g::forearm * cos(b) - g::lensOffset * sin(b),
            g::shoulder[1] + g::upperArm * sin(a) + g::forearm * sin(b) + g::lensOffset * cos(b),
            b,
        };
    }

    // Camera pose in the robot frame for q = [yaw, shoulder, elbow]:
    // {pos, forward, right, up}  (camera looks along forward, image right/up).
    CameraPose camera(const double q[3]) {
        const Planar p = planar(q[1], q[2]);
        const double c = cos(q[0] * DEG), s = sin(q[0] * DEG);
        const double cb = cos(p.pitch), sb = sin(p.pitch);
        return {
            {g::yawAxisX + p.rho * c, p.rho * s, p.z},
            {cb * c, cb * s, sb},
            {s, -c, 0},
            {-sb * c, -sb * s, cb},
        };
    }

    // Robot-frame point -> arm-plane coordinates {azimuth (rad), rho, z}.
    PlanePoint toPlane(const double p[3]) {
        const double dx = p[0] - g::yawAxisX, dy = p[1];
        return {atan2(dy, dx), hypot(dx, dy), p[2]};
    }

    // Analytic look-at IK: lens `dist` m from point p, looking straight at it.
    // The lens rides on the forearm axis, so this is a 2-link problem whose
    // second link is (forearm + dist, lensOffset), solved by the law of
    // cosines. Of the two elbow branches, the valid one closest to `prev`
    // (may be null) wins. -> [yaw, shoulder, elbow] (deg) in out; false if none.
    bool lookAt(const double p[3], double dist, const double* prev, double out[3]) {
        const PlanePoint t = toPlane(p);
        const double px = t.rho - g::shoulder[0], pz = t.z - g::shoulder[1];
        const double along = g::forearm + dist, L1 = g::upperArm;
        const double L2 = hypot(along, g::lensOffset), beta = atan2(g::lensOffset, along);
        const double c = (px * px + pz * pz - L1 * L1 - L2 * L2) / (2 * L1 * L2);
        if (c < -1 || c > 1) return false;
        bool found = false;
        double bestCost = INFINITY;
        const int signs[2] = {1, -1};
        for (int si = 0; si < 2; si++) {
            const double gamma = signs[si] * acos(c);
            const double a = atan2(pz, px) - atan2(L2 * sin(gamma), L1 + L2 * cos(gamma));
            const double b = a + gamma - beta;
            const double q[3] = {t.azimuth / DEG, a / DEG, (a - b) / DEG};
            if (!inRange(q[1], lim::shoulder) || !inRange(q[2], lim::elbow)) continue;
            if (planar(q[1], q[2]).z < lim::minCameraZ) continue;
            const double cost = prev ? fabs(q[1] - prev[1]) + fabs(q[2] - prev[2]) : 0;
            if (cost < bestCost) { out[0] = q[0]; out[1] = q[1]; out[2] = q[2]; bestCost = cost; found = true; }
        }
        return found;
    }

    // lookAt at `dist`, or at the nearest reachable distance in [minDist, maxDist].
    bool lookAtNearest(const double p[3], double dist, const double* prev, double minDist, double maxDist,
                       double outQ[3], double* outDist) {
        const double step = 0.005;
        for (int k = 0; k * step <= maxDist - minDist; k++) {
            const double candidates[2] = {dist + k * step, dist - k * step};
            for (int i = 0; i < (k == 0 ? 1 : 2); i++) {
                const double d = candidates[i];
                if (d < minDist || d > maxDist) continue;
                if (lookAt(p, d, prev, outQ)) { *outDist = d; return true; }
            }
        }
        return false;
    }
}

// ============================================================================
// Vision (RGB565 frames, high byte first, as the ESP32 camera driver gives them)
// ============================================================================
namespace Vision {
    namespace v = CONFIG::vision;

    struct Detection {
        bool found = false;
        double coverage = 0, cx = 0, cy = 0;
        bool touchesEdge = false;
        int width = 0, height = 0;
    };

    bool isOrange(int r, int g, int b) {
        if (r < v::minValue || r < g || g < b) return false;   // orange: r >= g >= b
        const double chroma = r - b;
        if (chroma / r < v::minSaturation) return false;
        const double hue = 60 * (g - b) / chroma;               // r is the max channel
        return hue >= v::minHueDeg && hue <= v::maxHueDeg;
    }

    // One RGB565 pixel, expanded to 8 bits per channel.
    static void pixel(const uint8_t* buf, int i, int* r, int* g, int* b) {
        const uint16_t c = (uint16_t)((buf[2 * i] << 8) | buf[2 * i + 1]);
        const int r5 = (c >> 11) & 0x1f, g6 = (c >> 5) & 0x3f, b5 = c & 0x1f;
        *r = (r5 << 3) | (r5 >> 2);
        *g = (g6 << 2) | (g6 >> 4);
        *b = (b5 << 3) | (b5 >> 2);
    }

    Detection detect(const uint8_t* buf, int w, int h) {
        const int s = v::stride;
        int count = 0, total = 0;
        double sx = 0, sy = 0;
        int minX = w, maxX = -1, minY = h, maxY = -1;
        for (int y = 0; y < h; y += s) {
            for (int x = 0; x < w; x += s) {
                total++;
                int r, g, b;
                pixel(buf, y * w + x, &r, &g, &b);
                if (!isOrange(r, g, b)) continue;
                count++; sx += x; sy += y;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }

        Detection d;
        d.found = count >= v::minSamples;
        d.coverage = (double)count / total;
        d.width = w;
        d.height = h;
        if (d.found) {
            d.cx = sx / count + 0.5 * (s - 1);
            d.cy = sy / count + 0.5 * (s - 1);
            d.touchesEdge = minX < s || minY < s || maxX >= w - s || maxY >= h - s;
        }
        return d;
    }
}

// ============================================================================
// Localizer: detection + camera pose -> ball point (robot frame)
// ============================================================================
namespace Localizer {
    namespace c = CONFIG::localizer;
    double range = c::initialRange;

    double focalPx(int h) { return (h / 2.0) / tan(CONFIG::lens::fovyDeg * DEG / 2); }

    void locate(const Vision::Detection& det, const Kinematics::CameraPose& cam, double out[3]) {
        const double f = focalPx(det.height);
        // Pinhole: apparent radius from the orange area -> distance. A blob
        // cut by the image edge looks too small: then it only proves the ball
        // is at least that close.
        const double rPx = sqrt(det.coverage * det.width * det.height / M_PI);
        const double measured = CONFIG::ballRadius * f / fmax(rPx, 0.5);
        if (!det.touchesEdge || measured < range) range += c::rangeSmoothing * (measured - range);
        const double u = (det.cx - det.width / 2.0) / f, v = -(det.cy - det.height / 2.0) / f;
        double ray[3];
        for (int k = 0; k < 3; k++) ray[k] = cam.forward[k] + u * cam.right[k] + v * cam.up[k];
        const double n = hypot3(ray);
        for (int k = 0; k < 3; k++) out[k] = cam.pos[k] + range * ray[k] / n;
    }
}

// ============================================================================
// BallFilter: alpha-beta filter on the ball point (robot frame). The ball's
// motion is estimated in a fixed frame, independent of how the arm moves.
// ============================================================================
namespace BallFilter {
    namespace c = CONFIG::ballFilter;
    bool hasPos = false;
    double pos[3] = {0, 0, 0}, vel[3] = {0, 0, 0};

    void update(const double z[3], double dt) {
        if (!hasPos || dt <= 0) {
            if (!hasPos) { for (int k = 0; k < 3; k++) pos[k] = z[k]; hasPos = true; }
            return;
        }
        for (int k = 0; k < 3; k++) {
            const double pred = pos[k] + vel[k] * dt;
            const double r = z[k] - pred;
            pos[k] = pred + c::alpha * r;
            vel[k] += (c::beta / dt) * r;
        }
        const double speed = hypot3(vel);
        if (speed > c::maxSpeed) for (int k = 0; k < 3; k++) vel[k] = vel[k] * c::maxSpeed / speed;
    }

    // Position `ahead` seconds after the last filtered sample; false if none.
    bool predict(double ahead, double out[3]) {
        if (!hasPos) return false;
        for (int k = 0; k < 3; k++) out[k] = pos[k] + vel[k] * ahead;
        return true;
    }

    void decay(double k) { for (int i = 0; i < 3; i++) vel[i] *= k; }
    void reset() { hasPos = false; for (int k = 0; k < 3; k++) vel[k] = 0; }
}

// ============================================================================
// PID:  u = ff*ballRate + kp*e + ki*int(e) + kd*(ballRate - ownRate)
//   e        error (what our motion should drive to 0)
//   ballRate how fast the ball alone moves e  (from BallFilter)
//   ownRate  how fast our joints move e        (from the Jacobian)
// so the D term is the true relative rate and nothing noisy is
// differentiated. Output clamped; conditional integration stops wind-up.
// u is the rate at which our motion should REDUCE e.
// ============================================================================
struct PID {
    CONFIG::PidConfig cfg;
    double integral = 0;
    double output = 0;

    explicit PID(const CONFIG::PidConfig& c) : cfg(c) {}

    double update(double e, double dt, double ballRate, double ownRate) {
        const double unsat = cfg.ff * ballRate + cfg.kp * e + cfg.ki * integral + cfg.kd * (ballRate - ownRate);
        const double u = clampd(unsat, -cfg.limit, cfg.limit);
        if (u == unsat || signOf(e) != signOf(unsat)) integral += e * dt;
        output = u;
        return u;
    }
    void reset() { integral = 0; output = 0; }
};

// ============================================================================
// Arm state (degrees), shared by Servoing, DiffIK and Arm.
// ============================================================================
struct ArmState { double q[3]; double qd[3]; };

// ============================================================================
// Servoing: ball estimate -> task
//   yawRate     rad/s, base turn
//   bearingRate rad/s, wanted change of the in-plane angle between the
//               camera axis and the ball (positive = ball moves up the axis)
//   posture     [shoulder, elbow] (deg) that looks at the ball from the
//               stand-off setpoint, if one is reachable
// ============================================================================
namespace Servoing {
    namespace c = CONFIG::servoing;
    namespace so = CONFIG::standoff;

    struct Measure { double azimuth, bearing, dist, z; };
    struct Jacobian { double bearing[2], z[2]; };
    struct Derivatives { Jacobian J; double ballAzimuth, ballBearing; };
    struct Task {
        double yawRate = 0, bearingRate = 0;
        bool hasPosture = false;
        double posture[2] = {0, 0};
        bool hasJ = false;
        Jacobian J = {{0, 0}, {0, 0}};
        double z = 0;
    };
    struct Errors { double azimuth, bearing, distance; };

    PID azimuth(c::azimuth), bearing(c::bearing), distance(c::distance);
    Task task;
    Errors errors = {0, 0, 0};
    bool hasCoverage = false;
    double coverage = 0;
    double standoff = so::initial;

    // Task values for joint angles q (deg) and ball point p (robot frame).
    // bearing: in-plane angle from the camera axis up to the ball.
    Measure measure(const double q[3], const double p[3]) {
        const Kinematics::PlanePoint ball = Kinematics::toPlane(p);
        const Kinematics::Planar cam = Kinematics::planar(q[1], q[2]);
        const double dr = ball.rho - cam.rho, dz = ball.z - cam.z;
        return {wrapPi(ball.azimuth - q[0] * DEG), wrapPi(atan2(dz, dr) - cam.pitch), hypot(dr, dz), cam.z};
    }

    // Partial derivatives by central differences: bearing and lens height
    // w.r.t. shoulder/elbow (per rad, for the IK and own rates), and the
    // azimuth/bearing rates caused by the ball's velocity alone.
    Derivatives derivatives(const double q[3], const double p[3], const double vel[3]) {
        const double h = 0.05;   // deg
        Derivatives d;
        for (int j = 0; j < 2; j++) {
            double qp[3] = {q[0], q[1], q[2]}, qm[3] = {q[0], q[1], q[2]};
            qp[j + 1] += h; qm[j + 1] -= h;
            const Measure a = measure(qp, p), b = measure(qm, p);
            const double k = 1 / (2 * h * DEG);
            d.J.bearing[j] = wrapPi(a.bearing - b.bearing) * k;
            d.J.z[j] = (a.z - b.z) * k;
        }
        const double dt = 0.01;
        double pp[3], pm[3];
        for (int k = 0; k < 3; k++) { pp[k] = p[k] + vel[k] * dt; pm[k] = p[k] - vel[k] * dt; }
        const Measure a = measure(q, pp), b = measure(q, pm);
        d.ballAzimuth = wrapPi(a.azimuth - b.azimuth) / (2 * dt);
        d.ballBearing = wrapPi(a.bearing - b.bearing) / (2 * dt);
        return d;
    }

    // ball, vel: predicted ball point now and its velocity; det: this frame
    // (for coverage; null while coasting), arm: current joint state.
    Task update(const Vision::Detection* det, const double ball[3], const double vel[3], double dt, const ArmState& arm) {
        const double* q = arm.q;
        const double* qd = arm.qd;
        const Measure m = measure(q, ball);
        const Derivatives d = derivatives(q, ball, vel);
        const Jacobian& J = d.J;

        // Azimuth: turning the base by +yawRate changes it at -yawRate.
        const double yawRate = azimuth.update(m.azimuth, dt, d.ballAzimuth, -qd[0] * DEG);

        // Bearing: target 0 (ball on the camera axis).
        const double ownBearing = (J.bearing[0] * qd[1] + J.bearing[1] * qd[2]) * DEG;
        const double uB = bearing.update(m.bearing, dt, d.ballBearing, ownBearing);

        // Distance: coverage -> relative distance error (coverage ~ 1/d^2), in
        // metres; > 0 means too much orange = too close. A blob cut by the
        // image edge under-reads, so it is only trusted when it says "too close".
        if (det && (!det->touchesEdge || det->coverage > so::targetCoverage)) {
            coverage = !hasCoverage ? det->coverage : coverage + c::coverageSmoothing * (det->coverage - coverage);
            hasCoverage = true;
        }
        double eD = 0;
        if (hasCoverage && coverage > 0) {
            const double rel = log(coverage / so::targetCoverage) / 2;
            const double band = log(1 + so::deadband) / 2;
            eD = fabs(rel) < band ? 0 : (rel - signOf(rel) * band) * m.dist;
        }
        // PI moves the stand-off setpoint: too close -> setpoint grows.
        standoff = clampd(standoff + distance.update(eD, dt, 0, 0) * dt, so::min, so::max);

        // Posture reference: look at the ball from the setpoint. If that is
        // out of reach, take the nearest reachable stand-off and move the
        // setpoint there too (no wind-up).
        double refQ[3], refDist = 0;
        const bool hasRef = Kinematics::lookAtNearest(ball, standoff, q, so::min, so::ikMax, refQ, &refDist);
        if (hasRef && refDist != standoff) standoff = clampd(refDist, so::min, so::max);

        errors = {m.azimuth, m.bearing, eD};
        task = Task();
        task.yawRate = yawRate;
        task.bearingRate = -uB;
        task.hasPosture = hasRef;
        if (hasRef) { task.posture[0] = refQ[1]; task.posture[1] = refQ[2]; }
        task.hasJ = true;
        task.J = J;
        task.z = m.z;
        return task;
    }

    void reset() {
        azimuth.reset(); bearing.reset(); distance.reset();
        hasCoverage = false;
        coverage = 0;
        task = Task();
    }
}

// ============================================================================
// DiffIK: task -> joint rates (deg/s)
//   yaw is decoupled: the base turn is the azimuth rate.
//   Shoulder/elbow, strict priority:
//     1. bearing:  q1 = W^-1 J1^T (J1 W^-1 J1^T + l^2)^-1 x1   (damped least squares)
//     2. posture:  qd = q1 + N * postureGain * (posture - q)
//        with N = I - W^-1 J1^T (J1 W^-1 J1^T)^-1 J1, the null space of task 1:
//        the arm reconfigures (and so changes distance) without disturbing the aim.
//   Near the floor, task 2 becomes "don't descend".
//   W grows as a joint approaches a limit, moving the work to the joint
//   that still has room. The result is scaled as a whole to the speed
//   limits, so its direction (and therefore task 1) survives saturation.
// ============================================================================
namespace DiffIK {
    namespace cfg = CONFIG::diffIk;
    namespace lim = CONFIG::limits;
    const double* const ranges[3] = {lim::yaw, lim::shoulder, lim::elbow};
    const double* const vMax = CONFIG::arm::maxVel;

    double weight(int i, double q, double rate) {
        const double* r = ranges[i];
        const double room = rate > 0 ? r[1] - q : q - r[0];
        if (room >= cfg::limitMargin) return 1;
        const double k = cfg::limitMargin / fmax(room, 0.5);
        return k * k;
    }

    // Task 1 solution and its null-space projector, inverse weights wi.
    struct Primary { double q1[2]; double N[2][2]; };
    Primary primary(const double J1[2], double x1, const double wi[2]) {
        const double a = J1[0] * J1[0] * wi[0] + J1[1] * J1[1] * wi[1];
        const double k1 = x1 / (a + cfg::damping * cfg::damping);
        Primary p = {{wi[0] * J1[0] * k1, wi[1] * J1[1] * k1}, {{1, 0}, {0, 1}}};
        if (a > 1e-9) {
            p.N[0][0] = 1 - wi[0] * J1[0] * J1[0] / a;
            p.N[0][1] = -wi[0] * J1[0] * J1[1] / a;
            p.N[1][0] = -wi[1] * J1[1] * J1[0] / a;
            p.N[1][1] = 1 - wi[1] * J1[1] * J1[1] / a;
        }
        return p;
    }

    static void project(const double N[2][2], const double v[2], double out[2]) {
        out[0] = N[0][0] * v[0] + N[0][1] * v[1];
        out[1] = N[1][0] * v[0] + N[1][1] * v[1];
    }

    // Task 2 = joint-space velocity v2 (rad/s), in the null space.
    void withPosture(const double J1[2], double x1, const double v2[2], const double wi[2], double out[2]) {
        const Primary p = primary(J1, x1, wi);
        double n[2];
        project(p.N, v2, n);
        out[0] = p.q1[0] + n[0];
        out[1] = p.q1[1] + n[1];
    }

    // Task 2 = one more task row J2 . qd = x2, in the null space.
    void withTask(const double J1[2], double x1, const double J2[2], double x2, const double wi[2], double out[2]) {
        const Primary p = primary(J1, x1, wi);
        const double J2n[2] = {J2[0] * p.N[0][0] + J2[1] * p.N[1][0], J2[0] * p.N[0][1] + J2[1] * p.N[1][1]};
        const double r2 = x2 - (J2[0] * p.q1[0] + J2[1] * p.q1[1]);
        const double b = J2n[0] * J2n[0] * wi[0] + J2n[1] * J2n[1] * wi[1];
        const double k2 = r2 / (b + cfg::secondaryDamping * cfg::secondaryDamping);
        const double v[2] = {wi[0] * J2n[0] * k2, wi[1] * J2n[1] * k2};
        double n[2];
        project(p.N, v, n);
        out[0] = p.q1[0] + n[0];
        out[1] = p.q1[1] + n[1];
    }

    // Solve once unweighted to know which way each joint moves, then again
    // with the joint-limit weights for that direction.
    template <class Solver>
    void weighted(Solver solver, const double q[3], double out[2]) {
        const double ones[2] = {1, 1};
        double first[2];
        solver(ones, first);
        const double wi[2] = {1 / weight(1, q[1], first[0]), 1 / weight(2, q[2], first[1])};
        solver(wi, out);
    }

    void solve(const Servoing::Task& task, const ArmState& arm, double out[3]) {
        const double* q = arm.q;
        double yaw = (task.yawRate / DEG) / weight(0, q[0], task.yawRate);
        yaw = clampd(yaw, -vMax[0], vMax[0]);
        if (!task.hasJ) { out[0] = yaw; out[1] = 0; out[2] = 0; return; }
        const Servoing::Jacobian& J = task.J;

        double v2[2] = {0, 0};
        if (task.hasPosture) {
            v2[0] = cfg::postureGain * (task.posture[0] - q[1]) * DEG;
            v2[1] = cfg::postureGain * (task.posture[1] - q[2]) * DEG;
        }
        double sol[2];
        weighted([&](const double wi[2], double o[2]) { withPosture(J.bearing, task.bearingRate, v2, wi, o); }, q, sol);

        // Floor guard: below minCameraZ + floorMargin, the lens may not sink
        // faster than floorGain * (height above minCameraZ).
        const double room = task.z - lim::minCameraZ;
        const double zRate = J.z[0] * sol[0] + J.z[1] * sol[1];
        const double minZRate = -cfg::floorGain * room;
        if (room < cfg::floorMargin && zRate < minZRate) {
            weighted([&](const double wi[2], double o[2]) { withTask(J.bearing, task.bearingRate, J.z, minZRate, wi, o); }, q, sol);
        }

        const double qd[2] = {sol[0] / DEG, sol[1] / DEG};
        const double scale = fmin(1, fmin(vMax[1] / fmax(fabs(qd[0]), 1e-9), vMax[2] / fmax(fabs(qd[1]), 1e-9)));
        out[0] = yaw;
        out[1] = qd[0] * scale;
        out[2] = qd[1] * scale;
    }
}

// ============================================================================
// Arm: commanded joint state, integrated from the joint-rate setpoint under
// velocity and acceleration limits (trapezoidal motion). Degrees.
// ============================================================================
namespace Arm {
    Servo yaw, shoulder, elbow;
    Servo* const sv[3] = {&yaw, &shoulder, &elbow};
    const double* const lims[3] = {CONFIG::limits::yaw, CONFIG::limits::shoulder, CONFIG::limits::elbow};
    const double* const vMax = CONFIG::arm::maxVel;
    const double* const aMax = CONFIG::arm::maxAcc;

    ArmState state = {{0, 0, 0}, {0, 0, 0}};
    double qdTarget[3] = {0, 0, 0};

    // Commanded joint angles of the last ticks, so a camera frame can be
    // paired with the pose the arm had when it was exposed (no feedback: the
    // command is the best estimate; the servos lag it a little).
    const int HISTORY_LEN = 64;
    double history[HISTORY_LEN][3];
    int historyCount = 0, historyHead = 0;   // ring buffer: head = next write

    // MG996R: 1000..2000 us for -90..+90 degrees (0 = centre).
    void writeServo(int i, double q) { sv[i]->writeMicroseconds((int)lround(1500 + q * 1000.0 / 180.0)); }

    void start() {
        yaw.attach(YAW_SIGNAL, 1000, 2000);
        shoulder.attach(SHOULDER_SIGNAL, 1000, 2000);
        elbow.attach(ELBOW_SIGNAL, 1000, 2000);
        for (int i = 0; i < 3; i++) { state.q[i] = CONFIG::startPose[i]; state.qd[i] = 0; writeServo(i, state.q[i]); }
    }

    void tick(double dt) {
        if (dt <= 0) return;
        for (int i = 0; i < 3; i++) {
            const double want = clampd(qdTarget[i], -vMax[i], vMax[i]);
            state.qd[i] += clampd(want - state.qd[i], -aMax[i] * dt, aMax[i] * dt);
            double q = state.q[i] + state.qd[i] * dt;
            if (q < lims[i][0] || q > lims[i][1]) { q = clampd(q, lims[i][0], lims[i][1]); state.qd[i] = 0; }
            state.q[i] = q;
            writeServo(i, q);
        }
        for (int k = 0; k < 3; k++) history[historyHead][k] = state.q[k];
        historyHead = (historyHead + 1) % HISTORY_LEN;
        if (historyCount < HISTORY_LEN) historyCount++;
    }

    // Commanded joint angles `age` ticks ago.
    void past(double age, double out[3]) {
        if (!historyCount) { for (int k = 0; k < 3; k++) out[k] = state.q[k]; return; }
        int back = (int)floor(age + 0.5);
        if (back > historyCount - 1) back = historyCount - 1;
        if (back < 0) back = 0;
        const int idx = (historyHead - 1 - back + HISTORY_LEN) % HISTORY_LEN;
        for (int k = 0; k < 3; k++) out[k] = history[idx][k];
    }

    void setRates(const double qd[3]) { for (int k = 0; k < 3; k++) qdTarget[k] = qd[k]; }
}

// ============================================================================
// Clock: simulated seconds per loop() tick, from millis() (sim time).
// ============================================================================
namespace Clock {
    bool hasTick = false, hasFrame = false;
    unsigned long lastTickMs = 0;
    double lastFrameTime = 0;
    double tickDt = CONFIG::loopMs / 1000.0;

    // -> sim seconds since the previous tick
    double tick() {
        const unsigned long now = millis();
        if (hasTick) tickDt = fmax(0.001, (now - lastTickMs) / 1000.0);
        lastTickMs = now;
        hasTick = true;
        return tickDt;
    }
    double ticksFor(double seconds) { return seconds / tickDt; }
    // -> sim seconds since the previous frame
    double frame(double time) {
        const double dt = hasFrame && time > lastFrameTime ? time - lastFrameTime : 0;
        lastFrameTime = time;
        hasFrame = true;
        return dt;
    }
}

// ============================================================================
// Main
// ============================================================================
// The follower's state and steps live in a namespace: the Arduino IDE writes a
// prototype for every top-level function at the top of the sketch, before the
// types above exist, and a function taking an ArmState there won't compile.
namespace Follower {
int lostFrames = 1000;          // start in search mode
    int frames = 0;
    bool hasHome = false;
    double home[3] = {0, 0, 0};
    int sweepDir = 1;
    int lastSide = 1;               // +1: ball last seen left of centre (turn CCW), -1: right
    bool cameraReady = false;
    unsigned long nextFrameMs = 0;  // when the next camera frame should be ready
    
    void startCamera() {
        camera_config_t config = {};
        config.pin_pwdn = CAMERA_PWDN;
        config.pin_reset = CAMERA_RESET;
        config.pin_xclk = CAMERA_XCLK;
        config.pin_sccb_sda = CAMERA_SDA;
        config.pin_sccb_scl = CAMERA_SCL;
        config.pin_d7 = CAMERA_D7; config.pin_d6 = CAMERA_D6; config.pin_d5 = CAMERA_D5; config.pin_d4 = CAMERA_D4;
        config.pin_d3 = CAMERA_D3; config.pin_d2 = CAMERA_D2; config.pin_d1 = CAMERA_D1; config.pin_d0 = CAMERA_D0;
        config.pin_vsync = CAMERA_VSYNC;
        config.pin_href = CAMERA_HREF;
        config.pin_pclk = CAMERA_PCLK;
        config.xclk_freq_hz = 20000000;
        config.ledc_timer = LEDC_TIMER_0;
        config.ledc_channel = LEDC_CHANNEL_0;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QQVGA;     // 160x120
        config.fb_count = 1;
        const esp_err_t err = esp_camera_init(&config);
        cameraReady = err == ESP_OK;
        if (!cameraReady) Serial.printf("follower: camera init failed (0x%x)\n", err);
    }

    // Lost: ease shoulder/elbow back to the home posture, then sweep the base.
    void searchRates(const ArmState& arm, double qd[3]) {
        namespace s = CONFIG::search;
        if (arm.q[0] > s::span[1]) sweepDir = -1;
        if (arm.q[0] < s::span[0]) sweepDir = 1;
        qd[0] = sweepDir * s::yawRate; qd[1] = 0; qd[2] = 0;
        if (hasHome) {
            qd[1] = s::homeGain * (home[1] - arm.q[1]);
            qd[2] = s::homeGain * (home[2] - arm.q[2]);
            // Sweep only once the camera looks at the floor in front: fade the
            // yaw in over the last 10 deg of posture error.
            const double off = fmax(fabs(home[1] - arm.q[1]), fabs(home[2] - arm.q[2]));
            qd[0] *= clampd(1 - (off - 5) / 10, 0, 1);
        }
    }
    
    // Track the predicted ball (also used, with a decaying velocity, to coast
    // through a few missed frames).
    void track(const Vision::Detection* det, double dt) {
        double ball[3];
        if (!BallFilter::predict(CONFIG::ballFilter::predict, ball)) return;
        const Servoing::Task task = Servoing::update(det, ball, BallFilter::vel, dt, Arm::state);
        double qd[3];
        DiffIK::solve(task, Arm::state, qd);
        Arm::setRates(qd);
    }
    
    void processFrame(const camera_fb_t* fb) {
        frames++;
        const double time = fb->timestamp.tv_sec + fb->timestamp.tv_usec / 1e6;
        const double dt = Clock::frame(time);
        const Vision::Detection det = Vision::detect(fb->buf, (int)fb->width, (int)fb->height);
    
        if (det.found) {
            if (lostFrames > CONFIG::lost::coastFrames) {
                Servoing::reset(); BallFilter::reset();
                Serial.println("follower: ball found");
            }
            lostFrames = 0;
            lastSide = det.cx < det.width / 2.0 ? 1 : -1;   // image right = clockwise
            // The frame shows the scene one camera latency ago: localize from
            // the pose the arm had then.
            double exposed[3], point[3];
            Arm::past(Clock::ticksFor(CONFIG::lens::latency), exposed);
            Localizer::locate(det, Kinematics::camera(exposed), point);
            BallFilter::update(point, dt);
            track(&det, dt);
        } else {
            lostFrames++;
            double predicted[3];
            if (lostFrames <= CONFIG::lost::coastFrames && BallFilter::predict(dt, predicted)) {
                BallFilter::update(predicted, dt);   // no measurement: follow the prediction
                BallFilter::decay(CONFIG::lost::decay);
                track(nullptr, dt);
            } else if (lostFrames <= CONFIG::lost::searchAfterFrames) {
                if (lostFrames == CONFIG::lost::coastFrames + 1) Serial.println("follower: ball lost, re-acquiring");
                const double qd[3] = {lastSide * CONFIG::lost::reacquireRate, 0, 0};
                Arm::setRates(qd);
            } else {
                if (lostFrames == CONFIG::lost::searchAfterFrames + 1) {
                    sweepDir = lastSide;
                    Serial.println("follower: searching");
                }
                double qd[3];
                searchRates(Arm::state, qd);
                Arm::setRates(qd);
            }
        }
    
        if (det.found && frames % CONFIG::logEvery == 0) {
            const Servoing::Errors& e = Servoing::errors;
            Serial.printf("az %.1f deg  bearing %.1f deg  cov %.1f%%  range %.1f cm  stand-off %.1f cm\n",
                          e.azimuth / DEG, e.bearing / DEG, 100 * det.coverage, 100 * Localizer::range, 100 * Servoing::standoff);
        }
    }
}

// Called once before the first loop().
void setup() {
    Serial.begin(115200);
    Arm::start();
    Follower::hasHome = Kinematics::lookAt(CONFIG::search::homeLookAt, CONFIG::search::homeDistance, nullptr, Follower::home);
    if (!Follower::hasHome) Serial.println("follower: home pose unreachable, check CONFIG::search");
    Follower::startCamera();
    Serial.println("follower: started");
}

void loop() {
    // Ask for a frame only once it is due: esp_camera_fb_get() waits for the
    // next one, which would stall the arm for up to a frame period.
    if (Follower::cameraReady && millis() >= Follower::nextFrameMs) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            Follower::processFrame(fb);
            esp_camera_fb_return(fb);
        }
        Follower::nextFrameMs = millis() + (unsigned long)CONFIG::lens::framePeriodMs - 3;
    }
    Arm::tick(Clock::tick());
    delay(CONFIG::loopMs);
}
