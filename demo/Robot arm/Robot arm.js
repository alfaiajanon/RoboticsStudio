// RoboticsStudio Controller Script -- orange ball follower (closed-loop)
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
//   comp_1   base yaw   (axis +Z, positive = counter-clockwise seen from above)
//   comp_6   shoulder   (positive = upper arm up)
//   comp_11  elbow      (positive = forearm bends down)
//   comp_17  OV7670 camera on the forearm tip, looking along the forearm
// With no wrist, the camera's pitch is (shoulder - elbow).
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

// ============================================================================
// CONFIG
// ============================================================================
const CONFIG = {
    servos: { yaw: comp_1, shoulder: comp_6, elbow: comp_11 },
    camera: comp_17,

    // Arm geometry, from the .rsdef connectors chained as in Robot arm.rsproj.
    // Planar coordinates (rho, z): rho = horizontal distance from the yaw axis.
    geometry: {
        yawAxisX: 0.01,              // yaw axis is the vertical line x = 0.01, y = 0
        shoulder: [0.0096, 0.0681],  // shoulder joint (rho, z)
        upperArm: 0.100,             // shoulder -> elbow
        forearm: 0.1352,             // elbow -> camera lens, along the forearm
        lensOffset: -0.0073,         // lens offset perpendicular to the forearm (up = +)
    },

    // Degrees. Commands are hard-clamped to these; DiffIK steers away from them.
    limits: {
        yaw: [-88, 88],
        shoulder: [-85, 85],
        elbow: [-85, 85],
        minCameraZ: 0.035,           // keep the lens off the floor (m)
    },

    // OV7670 device: 160x120, fovy 45 deg, latency_ms 60
    lens: { fovyDeg: 45, latency: 0.06 },
    ballRadius: 0.015,               // stage prop sp1, turns coverage into range

    vision: {
        stride: 2,                   // sample every 2nd pixel in x and y
        minHueDeg: 10, maxHueDeg: 50,
        minSaturation: 0.45,
        minValue: 60,                // 0..255
        minSamples: 3,               // fewer orange samples = not seen
    },

    localizer: {
        rangeSmoothing: 0.4,         // low-pass on the coverage-based range
        initialRange: 0.2,
    },

    ballFilter: {
        alpha: 0.5,                  // position correction per frame
        beta: 0.1,                   // velocity correction per frame
        maxSpeed: 1.0,               // m/s, clamp on the velocity estimate
        predict: 0.10,               // s ahead: camera latency + about half a frame
    },

    // PID per task:  u = ff*ballRate + kp*e + ki*int(e) + kd*(ballRate - ownRate)
    //   kp [1/s], ki [1/s^2], kd [-], ff 0..1, limit: output clamp.
    servoing: {
        azimuth:  { kp: 3.0, ki: 0.3, kd: 0.15, ff: 0.8, limit: 80 * Math.PI / 180 }, // rad/s
        bearing:  { kp: 3.0, ki: 0.3, kd: 0.15, ff: 0.8, limit: 70 * Math.PI / 180 }, // rad/s
        // PI on the stand-off setpoint: error in m (from coverage), output m/s.
        distance: { kp: 2.0, ki: 0.2, kd: 0, ff: 0, limit: 0.10 },
        coverageSmoothing: 0.4,      // low-pass on coverage
    },

    // The servos' +-90 deg range leaves a narrow window of reachable
    // stand-offs: for a ball on the floor 0.30 m out about 0.07-0.14 m,
    // 0.50 m out 0.26-0.34 m. Outside it, the nearest reachable one is used.
    standoff: {
        targetCoverage: 0.06,        // fraction of the image that should be orange (~0.11 m)
        deadband: 0.15,              // +-15 % around the target: hold distance
        initial: 0.11,
        min: 0.05,
        max: 0.25,
        ikMax: 0.60,                 // the look-at may go this far to keep the ball in view
    },

    diffIk: {
        damping: 0.05,               // DLS lambda for the bearing task (rad)
        secondaryDamping: 0.02,      // DLS lambda for the floor task (m)
        postureGain: 3.0,            // 1/s: how fast the arm glides to the posture reference
        limitMargin: 15,             // deg: joints get "heavier" this close to a limit
        floorGain: 4,                // 1/s: how hard the lens is kept above minCameraZ
        floorMargin: 0.02,           // m: floor guard starts this far above minCameraZ
    },

    arm: {
        maxVel: [80, 70, 80],        // deg/s  yaw, shoulder, elbow
        maxAcc: [300, 250, 300],     // deg/s^2
    },

    // Ball not seen:
    //   1..coastFrames             dropped frame? keep tracking the prediction
    //   ..searchAfterFrames        re-acquire: turn towards the edge it left by
    //   later                      full search sweep from the home posture
    lost: {
        coastFrames: 2,
        decay: 0.7,                  // per coasted frame, on the ball velocity
        reacquireRate: 60,           // deg/s
        searchAfterFrames: 30,       // ~2 s at 15 fps
    },

    search: {
        yawRate: 35,                 // deg/s sweep
        span: [-80, 80],
        homeGain: 2.0,               // 1/s: shoulder/elbow pulled back to the home posture
        homeLookAt: [0.30, 0.0, 0.015], homeDistance: 0.11,
    },

    loopMs: 15,
    logEvery: 15,                    // frames
};

const DEG = Math.PI / 180;
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const inRange = (v, r) => v >= r[0] && v <= r[1];
const wrapPi = (a) => Math.atan2(Math.sin(a), Math.cos(a));

// ============================================================================
// Kinematics (degrees in, metres/radians out)
// ============================================================================
const Kinematics = (function () {
    const g = CONFIG.geometry;
    const lim = CONFIG.limits;

    // Lens in the arm plane: {rho, z, pitch (rad)}.
    function planar(sh, el) {
        const a = sh * DEG, b = (sh - el) * DEG;    // upper arm angle, forearm angle (= camera pitch)
        return {
            rho: g.shoulder[0] + g.upperArm * Math.cos(a) + g.forearm * Math.cos(b) - g.lensOffset * Math.sin(b),
            z:   g.shoulder[1] + g.upperArm * Math.sin(a) + g.forearm * Math.sin(b) + g.lensOffset * Math.cos(b),
            pitch: b,
        };
    }

    // Camera pose in the robot frame for q = [yaw, shoulder, elbow]:
    // {pos, forward, right, up}  (camera looks along forward, image right/up).
    function camera(q) {
        const p = planar(q[1], q[2]);
        const c = Math.cos(q[0] * DEG), s = Math.sin(q[0] * DEG);
        const cb = Math.cos(p.pitch), sb = Math.sin(p.pitch);
        return {
            pos: [g.yawAxisX + p.rho * c, p.rho * s, p.z],
            forward: [cb * c, cb * s, sb],
            right: [s, -c, 0],
            up: [-sb * c, -sb * s, cb],
        };
    }

    // Robot-frame point -> arm-plane coordinates {azimuth (rad), rho, z}.
    function toPlane(p) {
        const dx = p[0] - g.yawAxisX, dy = p[1];
        return { azimuth: Math.atan2(dy, dx), rho: Math.hypot(dx, dy), z: p[2] };
    }

    // Analytic look-at IK: lens `dist` m from point p, looking straight at it.
    // The lens rides on the forearm axis, so this is a 2-link problem whose
    // second link is (forearm + dist, lensOffset), solved by the law of
    // cosines. Of the two elbow branches, the valid one closest to `prev`
    // wins. -> [yaw, shoulder, elbow] (deg) or null.
    function lookAt(p, dist, prev) {
        const t = toPlane(p);
        const px = t.rho - g.shoulder[0], pz = t.z - g.shoulder[1];
        const along = g.forearm + dist, L1 = g.upperArm;
        const L2 = Math.hypot(along, g.lensOffset), beta = Math.atan2(g.lensOffset, along);
        const c = (px * px + pz * pz - L1 * L1 - L2 * L2) / (2 * L1 * L2);
        if (c < -1 || c > 1) return null;
        let best = null, bestCost = Infinity;
        for (const sign of [1, -1]) {
            const gamma = sign * Math.acos(c);
            const a = Math.atan2(pz, px) - Math.atan2(L2 * Math.sin(gamma), L1 + L2 * Math.cos(gamma));
            const b = a + gamma - beta;
            const q = [t.azimuth / DEG, a / DEG, (a - b) / DEG];
            if (!inRange(q[1], lim.shoulder) || !inRange(q[2], lim.elbow)) continue;
            if (planar(q[1], q[2]).z < lim.minCameraZ) continue;
            const cost = prev ? Math.abs(q[1] - prev[1]) + Math.abs(q[2] - prev[2]) : 0;
            if (cost < bestCost) { best = q; bestCost = cost; }
        }
        return best;
    }

    // lookAt at `dist`, or at the nearest reachable distance in [minDist, maxDist].
    // -> {q, dist} or null.
    function lookAtNearest(p, dist, prev, minDist, maxDist) {
        const step = 0.005;
        for (let k = 0; k * step <= maxDist - minDist; k++) {
            for (const d of k === 0 ? [dist] : [dist + k * step, dist - k * step]) {
                if (d < minDist || d > maxDist) continue;
                const q = lookAt(p, d, prev);
                if (q) return { q: q, dist: d };
            }
        }
        return null;
    }

    return { planar: planar, camera: camera, toPlane: toPlane, lookAt: lookAt, lookAtNearest: lookAtNearest };
})();

// ============================================================================
// Vision
// ============================================================================
const Vision = (function () {
    const v = CONFIG.vision;

    function isOrange(r, g, b) {
        if (r < v.minValue || r < g || g < b) return false;   // orange: r >= g >= b
        const chroma = r - b;
        if (chroma / r < v.minSaturation) return false;
        const hue = 60 * (g - b) / chroma;                     // r is the max channel
        return hue >= v.minHueDeg && hue <= v.maxHueDeg;
    }

    // -> {found, coverage, cx, cy, touchesEdge, width, height}
    function detect(frame) {
        const w = frame.width, h = frame.height;
        const px = new Uint8Array(frame.data);
        const s = v.stride;

        let count = 0, total = 0, sx = 0, sy = 0;
        let minX = w, maxX = -1, minY = h, maxY = -1;
        for (let y = 0; y < h; y += s) {
            let i = y * w * 3;
            for (let x = 0; x < w; x += s, i += 3 * s) {
                total++;
                if (!isOrange(px[i], px[i + 1], px[i + 2])) continue;
                count++; sx += x; sy += y;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }

        const result = { found: count >= v.minSamples, coverage: count / total, width: w, height: h };
        if (result.found) {
            result.cx = sx / count + 0.5 * (s - 1);
            result.cy = sy / count + 0.5 * (s - 1);
            result.touchesEdge = minX < s || minY < s || maxX >= w - s || maxY >= h - s;
        }
        return result;
    }

    return { detect: detect };
})();

// ============================================================================
// Localizer: detection + camera pose -> ball point (robot frame)
// ============================================================================
const Localizer = (function () {
    const c = CONFIG.localizer;
    let range = c.initialRange;

    function focalPx(h) { return (h / 2) / Math.tan(CONFIG.lens.fovyDeg * DEG / 2); }

    function locate(det, cam) {
        const f = focalPx(det.height);
        // Pinhole: apparent radius from the orange area -> distance. A blob
        // cut by the image edge looks too small: then it only proves the ball
        // is at least that close.
        const rPx = Math.sqrt(det.coverage * det.width * det.height / Math.PI);
        const measured = CONFIG.ballRadius * f / Math.max(rPx, 0.5);
        if (!det.touchesEdge || measured < range) range += c.rangeSmoothing * (measured - range);
        const u = (det.cx - det.width / 2) / f, v = -(det.cy - det.height / 2) / f;
        const ray = [0, 1, 2].map(k => cam.forward[k] + u * cam.right[k] + v * cam.up[k]);
        const n = Math.hypot(ray[0], ray[1], ray[2]);
        return [0, 1, 2].map(k => cam.pos[k] + range * ray[k] / n);
    }

    return { locate: locate, range: () => range };
})();

// ============================================================================
// BallFilter: alpha-beta filter on the ball point (robot frame). The ball's
// motion is estimated in a fixed frame, independent of how the arm moves.
// ============================================================================
const BallFilter = (function () {
    const c = CONFIG.ballFilter;
    let pos = null, vel = [0, 0, 0];

    function update(z, dt) {
        if (!pos || dt <= 0) { if (!pos) pos = z.slice(); return; }
        for (let k = 0; k < 3; k++) {
            const pred = pos[k] + vel[k] * dt;
            const r = z[k] - pred;
            pos[k] = pred + c.alpha * r;
            vel[k] += (c.beta / dt) * r;
        }
        const speed = Math.hypot(vel[0], vel[1], vel[2]);
        if (speed > c.maxSpeed) vel = vel.map(v => v * c.maxSpeed / speed);
    }

    // Position `ahead` seconds after the last filtered sample.
    function predict(ahead) { return pos ? [0, 1, 2].map(k => pos[k] + vel[k] * ahead) : null; }

    function decay(k) { vel = vel.map(v => v * k); }
    function reset() { pos = null; vel = [0, 0, 0]; }

    return { update: update, predict: predict, decay: decay, reset: reset, velocity: () => vel, position: () => pos };
})();

// ============================================================================
// PID:  u = ff*ballRate + kp*e + ki*int(e) + kd*(ballRate - ownRate)
//   e        error (what our motion should drive to 0)
//   ballRate how fast the ball alone moves e  (from BallFilter)
//   ownRate  how fast our joints move e        (from the Jacobian)
// so the D term is the true relative rate and nothing noisy is
// differentiated. Output clamped; conditional integration stops wind-up.
// u is the rate at which our motion should REDUCE e.
// ============================================================================
function PID(cfg) {
    this.cfg = cfg;
    this.integral = 0;
    this.output = 0;
}
PID.prototype.update = function (e, dt, ballRate, ownRate) {
    const c = this.cfg;
    const unsat = c.ff * ballRate + c.kp * e + c.ki * this.integral + c.kd * (ballRate - ownRate);
    const u = clamp(unsat, -c.limit, c.limit);
    if (u === unsat || Math.sign(e) !== Math.sign(unsat)) this.integral += e * dt;
    this.output = u;
    return u;
};
PID.prototype.reset = function () { this.integral = 0; this.output = 0; };

// ============================================================================
// Servoing: ball estimate -> task
//   yawRate     rad/s, base turn
//   bearingRate rad/s, wanted change of the in-plane angle between the
//               camera axis and the ball (positive = ball moves up the axis)
//   posture     [shoulder, elbow] (deg) that looks at the ball from the
//               stand-off setpoint, or null if none is reachable
// ============================================================================
const Servoing = (function () {
    const c = CONFIG.servoing;
    const azimuth = new PID(c.azimuth), bearing = new PID(c.bearing), distance = new PID(c.distance);
    const so = CONFIG.standoff;
    let task = { yawRate: 0, bearingRate: 0, posture: null };
    let errors = { azimuth: 0, bearing: 0, distance: 0 };
    let coverage = null;
    let standoff = so.initial;

    // Task values for joint angles q (deg) and ball point p (robot frame).
    // bearing: in-plane angle from the camera axis up to the ball.
    function measure(q, p) {
        const ball = Kinematics.toPlane(p);
        const cam = Kinematics.planar(q[1], q[2]);
        const dr = ball.rho - cam.rho, dz = ball.z - cam.z;
        return {
            azimuth: wrapPi(ball.azimuth - q[0] * DEG),
            bearing: wrapPi(Math.atan2(dz, dr) - cam.pitch),
            dist: Math.hypot(dr, dz),
            z: cam.z,
        };
    }

    // Partial derivatives by central differences: bearing and lens height
    // w.r.t. shoulder/elbow (per rad, for the IK and own rates), and the
    // azimuth/bearing rates caused by the ball's velocity alone.
    function derivatives(q, p, vel) {
        const h = 0.05;   // deg
        const J = { bearing: [0, 0], z: [0, 0] };
        for (let j = 0; j < 2; j++) {
            const qp = q.slice(), qm = q.slice();
            qp[j + 1] += h; qm[j + 1] -= h;
            const a = measure(qp, p), b = measure(qm, p);
            const k = 1 / (2 * h * DEG);
            J.bearing[j] = wrapPi(a.bearing - b.bearing) * k;
            J.z[j] = (a.z - b.z) * k;
        }
        const dt = 0.01;
        const a = measure(q, [0, 1, 2].map(k => p[k] + vel[k] * dt));
        const b = measure(q, [0, 1, 2].map(k => p[k] - vel[k] * dt));
        const ballRates = {
            azimuth: wrapPi(a.azimuth - b.azimuth) / (2 * dt),
            bearing: wrapPi(a.bearing - b.bearing) / (2 * dt),
        };
        return { J: J, ballRates: ballRates };
    }

    // ball, vel: predicted ball point now and its velocity; det: this frame
    // (for coverage), arm: current joint state.
    function update(det, ball, vel, dt, arm) {
        const q = arm.q, qd = arm.qd;
        const m = measure(q, ball);
        const d = derivatives(q, ball, vel);
        const J = d.J;

        // Azimuth: turning the base by +yawRate changes it at -yawRate.
        const yawRate = azimuth.update(m.azimuth, dt, d.ballRates.azimuth, -qd[0] * DEG);

        // Bearing: target 0 (ball on the camera axis).
        const ownBearing = (J.bearing[0] * qd[1] + J.bearing[1] * qd[2]) * DEG;
        const uB = bearing.update(m.bearing, dt, d.ballRates.bearing, ownBearing);

        // Distance: coverage -> relative distance error (coverage ~ 1/d^2), in
        // metres; > 0 means too much orange = too close. A blob cut by the
        // image edge under-reads, so it is only trusted when it says "too close".
        if (det && (!det.touchesEdge || det.coverage > so.targetCoverage)) {
            coverage = coverage === null ? det.coverage : coverage + c.coverageSmoothing * (det.coverage - coverage);
        }
        let eD = 0;
        if (coverage !== null && coverage > 0) {
            const rel = Math.log(coverage / so.targetCoverage) / 2;
            const band = Math.log(1 + so.deadband) / 2;
            eD = Math.abs(rel) < band ? 0 : (rel - Math.sign(rel) * band) * m.dist;
        }
        // PI moves the stand-off setpoint: too close -> setpoint grows.
        standoff = clamp(standoff + distance.update(eD, dt, 0, 0) * dt, so.min, so.max);

        // Posture reference: look at the ball from the setpoint. If that is
        // out of reach, take the nearest reachable stand-off and move the
        // setpoint there too (no wind-up).
        const ref = Kinematics.lookAtNearest(ball, standoff, q, so.min, so.ikMax);
        if (ref && ref.dist !== standoff) standoff = clamp(ref.dist, so.min, so.max);

        errors = { azimuth: m.azimuth, bearing: m.bearing, distance: eD };
        task = { yawRate: yawRate, bearingRate: -uB, posture: ref ? [ref.q[1], ref.q[2]] : null, J: J, z: m.z };
        return task;
    }

    function reset() {
        azimuth.reset(); bearing.reset(); distance.reset();
        coverage = null;
        task = { yawRate: 0, bearingRate: 0, posture: null };
    }

    return { update: update, reset: reset, task: () => task, errors: () => errors, standoff: () => standoff };
})();

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
const DiffIK = (function () {
    const cfg = CONFIG.diffIk;
    const lim = CONFIG.limits;
    const ranges = [lim.yaw, lim.shoulder, lim.elbow];
    const vMax = CONFIG.arm.maxVel;

    function weight(i, q, rate) {
        const r = ranges[i];
        const room = rate > 0 ? r[1] - q : q - r[0];
        if (room >= cfg.limitMargin) return 1;
        const k = cfg.limitMargin / Math.max(room, 0.5);
        return k * k;
    }

    // Task 1 solution and its null-space projector, inverse weights wi.
    function primary(J1, x1, wi) {
        const a = J1[0] * J1[0] * wi[0] + J1[1] * J1[1] * wi[1];
        const k1 = x1 / (a + cfg.damping * cfg.damping);
        const q1 = [wi[0] * J1[0] * k1, wi[1] * J1[1] * k1];
        let N = [[1, 0], [0, 1]];
        if (a > 1e-9) {
            N = [[1 - wi[0] * J1[0] * J1[0] / a, -wi[0] * J1[0] * J1[1] / a],
                 [-wi[1] * J1[1] * J1[0] / a, 1 - wi[1] * J1[1] * J1[1] / a]];
        }
        return { q1: q1, N: N };
    }

    const project = (N, v) => [N[0][0] * v[0] + N[0][1] * v[1], N[1][0] * v[0] + N[1][1] * v[1]];

    // Task 2 = joint-space velocity v2 (rad/s), in the null space.
    function withPosture(J1, x1, v2, wi) {
        const p = primary(J1, x1, wi);
        const n = project(p.N, v2);
        return [p.q1[0] + n[0], p.q1[1] + n[1]];
    }

    // Task 2 = one more task row J2 . qd = x2, in the null space.
    function withTask(J1, x1, J2, x2, wi) {
        const p = primary(J1, x1, wi);
        const J2n = [J2[0] * p.N[0][0] + J2[1] * p.N[1][0], J2[0] * p.N[0][1] + J2[1] * p.N[1][1]];
        const r2 = x2 - (J2[0] * p.q1[0] + J2[1] * p.q1[1]);
        const b = J2n[0] * J2n[0] * wi[0] + J2n[1] * J2n[1] * wi[1];
        const k2 = r2 / (b + cfg.secondaryDamping * cfg.secondaryDamping);
        const n = project(p.N, [wi[0] * J2n[0] * k2, wi[1] * J2n[1] * k2]);
        return [p.q1[0] + n[0], p.q1[1] + n[1]];
    }

    // Solve once unweighted to know which way each joint moves, then again
    // with the joint-limit weights for that direction.
    function weighted(solver, q) {
        const first = solver([1, 1]);
        return solver([1 / weight(1, q[1], first[0]), 1 / weight(2, q[2], first[1])]);
    }

    function solve(task, arm) {
        const q = arm.q, J = task.J;
        let yaw = (task.yawRate / DEG) / weight(0, q[0], task.yawRate);
        yaw = clamp(yaw, -vMax[0], vMax[0]);
        if (!J) return [yaw, 0, 0];

        const v2 = task.posture
            ? [cfg.postureGain * (task.posture[0] - q[1]) * DEG, cfg.postureGain * (task.posture[1] - q[2]) * DEG]
            : [0, 0];
        let sol = weighted(wi => withPosture(J.bearing, task.bearingRate, v2, wi), q);

        // Floor guard: below minCameraZ + floorMargin, the lens may not sink
        // faster than floorGain * (height above minCameraZ).
        const room = task.z - lim.minCameraZ;
        const zRate = J.z[0] * sol[0] + J.z[1] * sol[1];
        const minZRate = -cfg.floorGain * room;
        if (room < cfg.floorMargin && zRate < minZRate) {
            sol = weighted(wi => withTask(J.bearing, task.bearingRate, J.z, minZRate, wi), q);
        }

        const qd = [sol[0] / DEG, sol[1] / DEG];
        const scale = Math.min(1, vMax[1] / Math.max(Math.abs(qd[0]), 1e-9), vMax[2] / Math.max(Math.abs(qd[1]), 1e-9));
        return [yaw, qd[0] * scale, qd[1] * scale];
    }

    return { solve: solve };
})();

// ============================================================================
// Arm: commanded joint state, integrated from the joint-rate setpoint under
// velocity and acceleration limits (trapezoidal motion). Degrees.
// ============================================================================
const Arm = (function () {
    const sv = [CONFIG.servos.yaw, CONFIG.servos.shoulder, CONFIG.servos.elbow];
    const lims = [CONFIG.limits.yaw, CONFIG.limits.shoulder, CONFIG.limits.elbow];
    const vMax = CONFIG.arm.maxVel, aMax = CONFIG.arm.maxAcc;

    const state = { q: [0, 0, 0], qd: [0, 0, 0] };
    let qdTarget = [0, 0, 0];

    // Measured joint angles of the last ticks, so a camera frame can be
    // paired with the pose the arm had when it was exposed.
    const history = [];
    const HISTORY_LEN = 64;

    function syncFromServos() {
        for (let i = 0; i < 3; i++) { state.q[i] = sv[i].read(); state.qd[i] = 0; }
    }

    function tick(dt) {
        if (dt <= 0) return;
        for (let i = 0; i < 3; i++) {
            const want = clamp(qdTarget[i], -vMax[i], vMax[i]);
            state.qd[i] += clamp(want - state.qd[i], -aMax[i] * dt, aMax[i] * dt);
            let q = state.q[i] + state.qd[i] * dt;
            if (q < lims[i][0] || q > lims[i][1]) { q = clamp(q, lims[i][0], lims[i][1]); state.qd[i] = 0; }
            state.q[i] = q;
            sv[i].write(q);
        }
        history.push([sv[0].read(), sv[1].read(), sv[2].read()]);
        if (history.length > HISTORY_LEN) history.shift();
    }

    // Measured joint angles `age` ticks ago (falls back to the command).
    function past(age) {
        if (!history.length) return state.q.slice();
        return history[Math.max(0, history.length - 1 - Math.round(age))];
    }

    return { state: state, tick: tick, past: past, syncFromServos: syncFromServos, setRates: (qd) => { qdTarget = qd.slice(); } };
})();

// ============================================================================
// Clock: simulated seconds per loop() tick. delay() is wall time and the
// simulation may run slower or faster than real time, so the tick length is
// measured from the camera frame timestamps (sim time).
// ============================================================================
const Clock = (function () {
    let lastFrameTime = null, ticksSinceFrame = 0;
    let tickDt = CONFIG.loopMs / 1000;

    return {
        tick: () => { ticksSinceFrame++; return tickDt; },
        ticksFor: (seconds) => seconds / tickDt,
        // -> sim seconds since the previous frame
        frame: (time) => {
            let dt = 0;
            if (lastFrameTime !== null && time > lastFrameTime && ticksSinceFrame > 0) {
                dt = time - lastFrameTime;
                tickDt = clamp(0.8 * tickDt + 0.2 * dt / ticksSinceFrame, 0.001, 0.1);
            }
            lastFrameTime = time;
            ticksSinceFrame = 0;
            return dt;
        },
    };
})();

// ============================================================================
// Main
// ============================================================================
const cam = CONFIG.camera;
let initialised = false;
let lostFrames = 1000;          // start in search mode
let frames = 0;
let home = null;
let sweepDir = 1;
let lastSide = 1;               // +1: ball last seen left of centre (turn CCW), -1: right

// The MCU only calls loop(), so loop() calls setup() once.
function setup() {
    Arm.syncFromServos();
    home = Kinematics.lookAt(CONFIG.search.homeLookAt, CONFIG.search.homeDistance, null);
    if (!home) console.log("follower: home pose unreachable, check CONFIG.search");
    console.log("follower: started");
}

// Lost: ease shoulder/elbow back to the home posture, then sweep the base.
function searchRates(arm) {
    const s = CONFIG.search;
    if (arm.q[0] > s.span[1]) sweepDir = -1;
    if (arm.q[0] < s.span[0]) sweepDir = 1;
    const qd = [sweepDir * s.yawRate, 0, 0];
    if (home) {
        qd[1] = s.homeGain * (home[1] - arm.q[1]);
        qd[2] = s.homeGain * (home[2] - arm.q[2]);
        // Sweep only once the camera looks at the floor in front: fade the
        // yaw in over the last 10 deg of posture error.
        const off = Math.max(Math.abs(home[1] - arm.q[1]), Math.abs(home[2] - arm.q[2]));
        qd[0] *= clamp(1 - (off - 5) / 10, 0, 1);
    }
    return qd;
}

// Track the predicted ball (also used, with a decaying velocity, to coast
// through a few missed frames).
function track(det, dt) {
    const ball = BallFilter.predict(CONFIG.ballFilter.predict);
    Arm.setRates(DiffIK.solve(Servoing.update(det, ball, BallFilter.velocity(), dt, Arm.state), Arm.state));
}

function processFrame(frame) {
    frames++;
    const dt = Clock.frame(frame.time);
    const det = Vision.detect(frame);

    if (det.found) {
        if (lostFrames > CONFIG.lost.coastFrames) {
            Servoing.reset(); BallFilter.reset();
            console.log("follower: ball found");
        }
        lostFrames = 0;
        lastSide = det.cx < det.width / 2 ? 1 : -1;   // image right = clockwise
        // The frame shows the scene one camera latency ago: localize from
        // the pose the arm had then.
        const exposed = Arm.past(Clock.ticksFor(CONFIG.lens.latency));
        BallFilter.update(Localizer.locate(det, Kinematics.camera(exposed)), dt);
        track(det, dt);
    } else {
        lostFrames++;
        if (lostFrames <= CONFIG.lost.coastFrames && BallFilter.position()) {
            BallFilter.update(BallFilter.predict(dt), dt);   // no measurement: follow the prediction
            BallFilter.decay(CONFIG.lost.decay);
            track(null, dt);
        } else if (lostFrames <= CONFIG.lost.searchAfterFrames) {
            if (lostFrames === CONFIG.lost.coastFrames + 1) console.log("follower: ball lost, re-acquiring");
            Arm.setRates([lastSide * CONFIG.lost.reacquireRate, 0, 0]);
        } else {
            if (lostFrames === CONFIG.lost.searchAfterFrames + 1) {
                sweepDir = lastSide;
                console.log("follower: searching");
            }
            Arm.setRates(searchRates(Arm.state));
        }
    }

    if (det.found && frames % CONFIG.logEvery === 0) {
        const e = Servoing.errors();
        console.log("az " + (e.azimuth / DEG).toFixed(1) + " deg  bearing " + (e.bearing / DEG).toFixed(1)
            + " deg  cov " + (100 * det.coverage).toFixed(1) + "%  range " + (100 * Localizer.range()).toFixed(1)
            + " cm  stand-off " + (100 * Servoing.standoff()).toFixed(1) + " cm");
    }
}

function loop() {
    if (!initialised) { setup(); initialised = true; }

    if (cam.available()) {
        const frame = cam.capture();
        if (frame) processFrame(frame);
    }
    Arm.tick(Clock.tick());
    delay(CONFIG.loopMs);
}
