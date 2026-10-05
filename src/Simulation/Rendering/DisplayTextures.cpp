#include "DisplayTextures.h"

#include <GL/gl.h>

#include "Document/Components/ComponentBlueprint.h"
#include "Document/Components/ComponentInstance.h"
#include "Utils/Log.h"



void DisplayTextures::rebuild(ComponentInstance* root, const mjModel* m) {
    std::lock_guard<std::mutex> lock(entriesMutex);
    entries.clear();
    ++generation;
    if (m) addEntries(root, m);
}


void DisplayTextures::addEntries(ComponentInstance* comp, const mjModel* m) {
    if (!comp) return;
    if (ComponentBlueprint* bp = comp->getBlueprint()) {
        for (auto it = bp->interfaceInputs.constBegin(); it != bp->interfaceInputs.constEnd(); ++it) {
            if (it.value().target.kind != "display") continue;
            const QString texName = QString("comp_%1_%2_tex").arg(comp->uid).arg(it.value().target.id);

            Entry e;
            e.texId = mj_name2id(m, mjOBJ_TEXTURE, texName.toStdString().c_str());
            if (e.texId < 0) {
                Log::warning(QString("DisplayTextures: texture '%1' not in the model; signal '%2' not shown in 3D")
                                .arg(texName, it.key()));
                continue;
            }
            e.width = m->tex_width[e.texId];
            e.height = m->tex_height[e.texId];
            e.comp = comp;
            e.signal = it.key();
            entries.push_back(e);
        }
    }
    for (ComponentInstance* child : comp->children) addEntries(child, m);
}


void DisplayTextures::collect() {
    std::lock_guard<std::mutex> lock(entriesMutex);
    for (Entry& e : entries) {
        ImageIOValue img = e.comp->getImageValue(e.signal);
        if (img.seq == e.seq && (img.pixels != nullptr) == (e.pixels != nullptr)) continue;
        e.seq = img.pixels ? img.seq : 0;
        e.pixels = img.pixels;
    }
}


void DisplayTextures::upload(const mjrContext* con, UploadCache& cache) {
    struct Pending { int texId, width, height; quint64 seq; std::shared_ptr<const QByteArray> pixels; };
    std::vector<Pending> work;
    {
        std::lock_guard<std::mutex> lock(entriesMutex);
        if (cache.generation != generation) {
            cache.generation = generation;
            cache.seq.clear();   // fresh context: textures are dark again
        }
        for (const Entry& e : entries) {
            auto it = cache.seq.constFind(e.texId);
            const quint64 last = it == cache.seq.constEnd() ? 0 : it.value();
            if (it != cache.seq.constEnd() && last == e.seq) continue;
            if (it == cache.seq.constEnd() && e.seq == 0) { cache.seq.insert(e.texId, 0); continue; }   // already dark
            work.push_back({e.texId, e.width, e.height, e.seq, e.pixels});
        }
    }

    for (const Pending& p : work) {
        cache.seq.insert(p.texId, p.seq);
        if (p.texId < 0 || p.texId >= con->ntexture) continue;

        const qsizetype bytes = qsizetype(p.width) * p.height * 3;
        QByteArray dark;
        const char* data = nullptr;
        if (p.pixels && p.pixels->size() >= bytes) {
            data = p.pixels->constData();
        } else {
            dark = QByteArray(bytes, char(5));   // matches the MJCF texture's dark rgb1
            data = dark.constData();
        }

        glBindTexture(GL_TEXTURE_2D, con->texture[p.texId]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p.width, p.height, GL_RGB, GL_UNSIGNED_BYTE, data);
        // Crisp pixels, and no mipmaps to keep in sync with level 0.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
