#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <memory>
#include <mutex>
#include <vector>

#include "mujoco/mujoco.h"


class ComponentInstance;



/*
 * Puts display frames onto the screen quads in every GL context that
 * renders the simulation (main viewport, camera renderer).
 *
 * Each display's MJCF texture (comp_<uid>_<id>_tex, see
 * ComponentBlueprint::generateDisplayAssetXML) is overwritten directly in
 * each context's GL texture with glTexSubImage2D -- m->tex_data and
 * mjr_uploadTexture are not used, so no mjModel memory is shared across
 * threads, and renderers never touch ComponentInstance.
 *
 *   rebuild()  model reload, physicsMutex held: map display signals to
 *              texture ids, bump the generation.
 *   collect()  physics thread, physicsMutex held: take each display's
 *              newest ImageIOValue pixels (pointer copy).
 *   upload()   any GL thread with its context current: upload what changed
 *              since this context's last upload.
 */
class DisplayTextures {
    public:
        // One per GL context. Reset when the generation changes (the context
        // is rebuilt for every new model, with the textures back to dark).
        struct UploadCache {
            quint64 generation = 0;
            QHash<int, quint64> seq;   // texId -> seq last uploaded (0 = dark)
        };

        void rebuild(ComponentInstance* root, const mjModel* m);
        void collect();
        void upload(const mjrContext* con, UploadCache& cache);

    private:
        struct Entry {
            int texId = -1;
            int width = 0;
            int height = 0;
            ComponentInstance* comp = nullptr;   // physics thread only (rebuild/collect)
            QString signal;
            quint64 seq = 0;
            std::shared_ptr<const QByteArray> pixels;   // null = dark
        };

        std::mutex entriesMutex;
        std::vector<Entry> entries;
        quint64 generation = 1;

        void addEntries(ComponentInstance* comp, const mjModel* m);
};
