#include "studio_anim.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <unordered_map>

#include <QFile>
#include <QFileInfo>

#include "studio.h"
#include "bone_accessor.h"
#include "studiomdl/bone_setup.h"
#include "tier1/keyvalues.h"
#include "jigglebones.h"

namespace
{
struct AlignedDelete
{
    void operator()(std::byte *p) const { ::operator delete[](p, std::align_val_t(16)); }
};

// What mdlcache keeps per loaded model: the bytes, its .ani blocks, the autoplay list and, for models
// with $includemodel, the merged virtual model.
struct HostModel
{
    std::unique_ptr<std::byte[], AlignedDelete> mdl;
    QByteArray ani;
    std::vector<unsigned short> autoplay;
    std::unique_ptr<virtualmodel_t> vmodel;
    QString path;
    struct HostSet *set = nullptr;
    const studiohdr_t *hdr() const { return reinterpret_cast<const studiohdr_t *>(mdl.get()); }
};

// A viewer model and the include models FindModel pulled in for it.
struct HostSet
{
    QString gameRoot; // folder that holds models/, for $includemodel paths
    std::vector<std::unique_ptr<HostModel>> models;
};

std::unordered_map<const studiohdr_t *, HostModel *> g_hostModels;

QByteArray ReadFile(const QString &path, bool required)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (required)
            throw std::runtime_error((path + QStringLiteral(": ") + file.errorString()).toStdString());
        return {};
    }
    return file.readAll();
}

HostModel &LoadHostModel(HostSet &set, const QString &path)
{
    auto host = std::make_unique<HostModel>();
    const auto bytes = ReadFile(path, true);
    if (bytes.size() < qsizetype(sizeof(studiohdr_t)))
        throw std::runtime_error((path + QStringLiteral(": truncated MDL header")).toStdString());
    // Engine code reads the header and its tables in place, with SIMD loads on aligned data.
    host->mdl.reset(static_cast<std::byte *>(::operator new[](bytes.size(), std::align_val_t(16))));
    std::memcpy(host->mdl.get(), bytes.constData(), bytes.size());
    const auto *studio = host->hdr();
    if (studio->id != IDSTUDIOHEADER || studio->version != STUDIO_VERSION || studio->length != bytes.size())
        throw std::runtime_error((path + QStringLiteral(": expected an intact IDST MDL v49")).toStdString());
    // .ani blocks: the engine resolves szanimblockname under the game folder; the model's own folder holds them too.
    if (studio->numanimblocks > 1)
        host->ani = ReadFile(QFileInfo(path).absolutePath() + QLatin1Char('/') + QFileInfo(path).completeBaseName() +
                                 QStringLiteral(".ani"), false);
    for (int i = 0; i < studio->numlocalseq; ++i)
        if (HasFlag(studio->pLocalSeqdesc(i)->flags, StudioSequenceFlags::Autoplay))
            host->autoplay.push_back(static_cast<unsigned short>(i));
    host->path = QFileInfo(path).absoluteFilePath();
    host->set = &set;
    g_hostModels[studio] = host.get();
    set.models.push_back(std::move(host));
    return *set.models.back();
}

void ToQt(const matrix3x4_t &m, QMatrix4x4 &out)
{
    out = QMatrix4x4(m[0][0], m[0][1], m[0][2], m[0][3],
                     m[1][0], m[1][1], m[1][2], m[1][3],
                     m[2][0], m[2][1], m[2][2], m[2][3],
                     0, 0, 0, 1);
}

matrix3x4_t FromQt(const QMatrix4x4 &m)
{
    return matrix3x4_t(m(0, 0), m(0, 1), m(0, 2), m(0, 3),
                       m(1, 0), m(1, 1), m(1, 2), m(1, 3),
                       m(2, 0), m(2, 1), m(2, 2), m(2, 3));
}
} // namespace

// Host side of the engine's studiohdr_t <-> mdlcache link (engine: datacache/mdlcache.cpp).
virtualmodel_t *studiohdr_t::GetVirtualModel() const
{
    const auto host = g_hostModels.find(this);
    return host == g_hostModels.end() ? nullptr : host->second->vmodel.get();
}

const studiohdr_t *virtualgroup_t::GetStudioHdr() const
{
    return static_cast<const studiohdr_t *>(cache);
}

// $includemodel names are relative to the game folder ("models/player/anims.mdl").
const studiohdr_t *studiohdr_t::FindModel(void **cache, char const *modelname) const
{
    const auto host = g_hostModels.find(this);
    if (host == g_hostModels.end())
        return nullptr;
    auto &set = *host->second->set;
    const auto path = QFileInfo(set.gameRoot + QLatin1Char('/') + QString::fromUtf8(modelname)).absoluteFilePath();
    for (const auto &model : set.models)
        if (model->path.compare(path, Qt::CaseInsensitive) == 0)
            return static_cast<const studiohdr_t *>(*cache = const_cast<studiohdr_t *>(model->hdr()));
    try
    {
        const auto *found = LoadHostModel(set, path).hdr();
        *cache = const_cast<studiohdr_t *>(found);
        return found;
    }
    catch (const std::exception &error)
    {
        qWarning("Include model not loaded: %s", error.what());
        return nullptr;
    }
}

uint8_t *studiohdr_t::GetAnimBlock(int i, bool) const
{
    const auto host = g_hostModels.find(this);
    if (host == g_hostModels.end() || i <= 0 || i >= numanimblocks)
        return nullptr;
    const auto *block = pAnimBlock(i);
    auto &ani = host->second->ani;
    if (block->datastart < 0 || block->dataend < block->datastart || block->dataend > ani.size())
        return nullptr;
    return reinterpret_cast<uint8_t *>(ani.data()) + block->datastart;
}

bool studiohdr_t::hasAnimBlockBeenPreloaded(int) const
{
    return true; // the whole .ani file is loaded with the model
}

int studiohdr_t::GetAutoplayList(unsigned short **pOut) const
{
    const auto host = g_hostModels.find(this);
    if (host == g_hostModels.end())
        return 0;
    // mdlcache: a virtual model's merged list, else the model's own autoplay sequences
    if (const auto &vmodel = host->second->vmodel)
    {
        if (pOut)
            *pOut = vmodel->m_autoplaySequences.data();
        return vmodel->m_autoplaySequences.size();
    }
    if (pOut)
        *pOut = host->second->autoplay.data();
    return int(host->second->autoplay.size());
}

struct alignas(16) StudioAnimation::Impl
{
    struct Layer
    {
        int sequence = 0;
        float cycle = 0, weight = 0, playbackrate = 1;
        int priority = 0;
    };

    HostSet hosts;
    CStudioHdr hdr;
    float poseparameter[MAXSTUDIOPOSEPARAM] = {};
    float controller[MAXSTUDIOBONECTRLS] = {};
    int sequence = 0, prevsequence = 0;
    float cycle = 0, prevcycle = 0, sequencetime = 0, blendtime = DefaultBlendTime;
    Layer layers[MaxLayers];
    float realtime = 0, autoplaytime = 0;
    int framecounter = 0;
    float prevIKCycles[5] = {};
    CIKContext ik;
    CJiggleBones jiggle;
    // Repaints between clock ticks (camera drags) must not step the simulation: CJiggleBones clamps a zero
    // time step up to 1 ms, so each extra step turns the camera's motion into huge velocities.
    float jiggleTime = -1;
    matrix3x4a_t jiggleOffset[MAXSTUDIOBONES]; // goal-to-jiggled, from the last step
    BoneVector pos[MAXSTUDIOBONES];
    BoneQuaternionAligned q[MAXSTUDIOBONES];
    matrix3x4a_t boneToWorld[MAXSTUDIOBONES];

    ~Impl()
    {
        for (const auto &model : hosts.models)
            g_hostModels.erase(model->hdr());
    }

    float Duration(int seq) const { return Studio_Duration(&hdr, seq, poseparameter); }
    int MaxFrame(int seq) const { return Studio_MaxFrame(&hdr, seq, poseparameter); }

    // upstream StudioModel::GetMovement(prevcycle[5], ...)
    void GetMovement(float prevcycle[5], Vector &vecPos, QAngle &vecAngles)
    {
        vecPos.Init();
        vecAngles.Init();
        // assume that changes < -0.5 are loops....
        if (cycle - prevcycle[0] < -0.5)
            prevcycle[0] = prevcycle[0] - 1.0;
        Studio_SeqMovement(&hdr, sequence, prevcycle[0], cycle, poseparameter, vecPos, vecAngles);
        prevcycle[0] = cycle;
        for (int i = 0; i < 4; i++)
        {
            Vector vecTmp;
            QAngle angTmp;
            if (layers[i].cycle - prevcycle[i + 1] < -0.5)
                prevcycle[i + 1] = prevcycle[i + 1] - 1.0;
            if (layers[i].weight > 0.0)
            {
                vecTmp.Init();
                angTmp.Init();
                if (Studio_SeqMovement(&hdr, layers[i].sequence, prevcycle[i + 1], layers[i].cycle, poseparameter, vecTmp, angTmp))
                    vecPos = vecPos * (1.0 - layers[i].weight) + vecTmp * layers[i].weight;
            }
            prevcycle[i + 1] = layers[i].cycle;
        }
    }
};

StudioAnimation::StudioAnimation(const QString &mdlPath) : d(std::make_unique<Impl>())
{
    const auto absolute = QFileInfo(mdlPath).absoluteFilePath();
    const auto models = absolute.lastIndexOf(QStringLiteral("/models/"), -1, Qt::CaseInsensitive);
    d->hosts.gameRoot = models >= 0 ? absolute.left(models) : QFileInfo(absolute).absolutePath();
    auto &host = LoadHostModel(d->hosts, absolute);
    const auto *studio = host.hdr();
    // mdlcache::GetVirtualModel: group 0 is the model itself, AppendModels pulls in every $includemodel.
    if (studio->numincludemodels > 0)
    {
        host.vmodel = std::make_unique<virtualmodel_t>();
        host.vmodel->m_group.AddToTail();
        host.vmodel->m_group[0].cache = const_cast<studiohdr_t *>(studio);
        host.vmodel->AppendModels(0, studio);
    }
    d->hdr.Init(studio);

    // upstream initSequenceChoices starts every pose parameter at 0; controllers start zeroed.
    for (int i = 0; i < PoseParameterCount(); ++i)
        SetPoseParameter(i, 0);
}

StudioAnimation::~StudioAnimation() = default;

int StudioAnimation::BoneCount() const { return d->hdr.numbones(); }
int StudioAnimation::SequenceCount() const { return d->hdr.GetNumSeq(); }

QString StudioAnimation::SequenceName(int sequence) const
{
    return sequence >= 0 && sequence < SequenceCount() ? QString::fromUtf8(d->hdr.pSeqdesc(sequence).pszLabel()) : QString();
}

QString StudioAnimation::ActivityName(int sequence) const
{
    return sequence >= 0 && sequence < SequenceCount() ? QString::fromUtf8(d->hdr.pSeqdesc(sequence).pszActivityName()) : QString();
}

bool StudioAnimation::IsHidden(int sequence) const
{
    return sequence >= 0 && sequence < SequenceCount() && HasFlag(d->hdr.pSeqdesc(sequence).flags, StudioSequenceFlags::Hidden);
}

// upstream CreateSortedSequenceList's sort key
int StudioAnimation::FacePoserType(int sequence) const
{
    if (sequence < 0 || sequence >= SequenceCount())
        return 0;
    const char *text = Studio_GetKeyValueText(&d->hdr, sequence);
    if (!text)
        return 0;
    int type = 0;
    auto *keyValues = new KeyValues("sort");
    if (keyValues->LoadFromBuffer("mdl", text))
        if (auto *facePoser = keyValues->FindKey("faceposer"))
        {
            const auto value = facePoser->GetString("type", "");
            const auto type_ = QString::fromUtf8(value.data(), qsizetype(value.size()));
            if (type_.compare(QLatin1String("posture"), Qt::CaseInsensitive) == 0)
                type = 2;
            else if (type_.compare(QLatin1String("gesture"), Qt::CaseInsensitive) == 0)
                type = 1;
        }
    keyValues->deleteThis();
    return type;
}

QStringList StudioAnimation::ActivityModifiers(int sequence) const
{
    QStringList names;
    if (sequence < 0 || sequence >= SequenceCount())
        return names;
    const auto &desc = d->hdr.pSeqdesc(sequence);
    for (int i = 0; i < desc.numactivitymodifiers; i++)
        names.append(QString::fromUtf8(desc.pActivityModifier(i)->pszName()));
    return names;
}

int StudioAnimation::PoseParameterCount() const { return d->hdr.GetNumPoseParameters(); }

QString StudioAnimation::PoseParameterName(int parameter) const
{
    return parameter >= 0 && parameter < PoseParameterCount() ? QString::fromUtf8(d->hdr.pPoseParameter(parameter).pszName()) : QString();
}

bool StudioAnimation::GetPoseParameterRange(int parameter, float *min, float *max) const
{
    *min = *max = 0;
    if (parameter < 0 || parameter >= PoseParameterCount())
        return false;
    const auto &pose = d->hdr.pPoseParameter(parameter);
    *min = pose.start;
    *max = pose.end;
    return true;
}

float StudioAnimation::SetPoseParameter(int parameter, float value)
{
    if (parameter < 0 || parameter >= PoseParameterCount())
        return 0.0f;
    return Studio_SetPoseParameter(&d->hdr, parameter, value, d->poseparameter[parameter]);
}

float StudioAnimation::GetPoseParameter(int parameter) const
{
    if (parameter < 0 || parameter >= PoseParameterCount())
        return 0.0f;
    return Studio_GetPoseParameter(&d->hdr, parameter, d->poseparameter[parameter]);
}

int StudioAnimation::ControllerCount() const { return d->hdr.numbonecontrollers(); }

StudioAnimation::Controller StudioAnimation::GetController(int index) const
{
    if (index < 0 || index >= ControllerCount())
        return {};
    const auto *controller = d->hdr.pBonecontroller(index);
    return {controller->inputfield, controller->start, controller->end};
}

float StudioAnimation::SetController(int inputfield, float value)
{
    if (inputfield < 0 || inputfield >= MAXSTUDIOBONECTRLS)
        return 0.0f;
    return Studio_SetController(&d->hdr, inputfield, value, d->controller[inputfield]);
}

int StudioAnimation::SetSequence(int sequence)
{
    if (sequence < 0)
        return 0;
    if (sequence > SequenceCount())
        return d->sequence;
    d->prevsequence = d->sequence;
    d->sequence = sequence;
    d->cycle = 0;
    d->sequencetime = 0.0;
    return d->sequence;
}

int StudioAnimation::GetSequence() const { return d->sequence; }

int StudioAnimation::SetOverlaySequence(int layer, int sequence, float weight)
{
    if (layer < 0 || layer >= MaxLayers)
        return 0;
    if (sequence < 0 || sequence >= SequenceCount())
        sequence = 0;
    d->layers[layer].sequence = sequence;
    d->layers[layer].weight = weight;
    d->layers[layer].playbackrate = 1.0;
    return sequence;
}

int StudioAnimation::GetOverlaySequence(int layer) const
{
    return layer >= 0 && layer < MaxLayers ? d->layers[layer].sequence : 0;
}

void StudioAnimation::Advance(float realDt, float speedScale)
{
    d->realtime += realDt;
    d->autoplaytime += realDt * speedScale; // upstream GetAutoPlayTime

    // upstream StudioModel::AdvanceFrame
    float dt = realDt * speedScale;
    if (dt > 0.1)
        dt = 0.1f;
    float t = d->Duration(d->sequence);
    if (t > 0)
    {
        if (dt > 0)
        {
            d->cycle += dt / t;
            d->sequencetime += dt;
            d->cycle -= int(d->cycle); // wrap
        }
    }
    else
        d->cycle = 0;

    for (auto &layer : d->layers)
    {
        t = d->Duration(layer.sequence);
        if (t > 0)
        {
            if (dt > 0)
            {
                layer.cycle += (dt / t) * layer.playbackrate;
                layer.cycle -= int(layer.cycle);
            }
        }
        else
            layer.cycle = 0;
    }
}

float StudioAnimation::GetCycle(int layer) const
{
    if (layer == 0)
        return d->cycle;
    if (layer <= MaxLayers)
        return d->layers[layer - 1].cycle;
    return 0;
}

float StudioAnimation::GetFrame(int layer) const { return GetCycle(layer) * GetMaxFrame(layer); }

int StudioAnimation::GetMaxFrame(int layer) const
{
    if (layer == 0)
        return d->MaxFrame(d->sequence);
    if (layer <= MaxLayers)
        return d->MaxFrame(d->layers[layer - 1].sequence);
    return 0;
}

// upstream StudioModel::SetFrame(iLayer, frame)
int StudioAnimation::SetFrame(int layer, int frame)
{
    if (frame <= 0)
        frame = 0;
    const int maxFrame = GetMaxFrame(layer);
    float cycle = 0;
    if (maxFrame)
    {
        if (frame >= maxFrame)
        {
            frame = maxFrame;
            cycle = 0.99999;
        }
        cycle = frame / float(maxFrame);
    }
    if (layer == 0)
        d->cycle = cycle;
    else if (layer <= MaxLayers)
        d->layers[layer - 1].cycle = cycle;
    return frame;
}

float StudioAnimation::GetFPS() const { return Studio_FPS(&d->hdr, d->sequence, d->poseparameter); }
float StudioAnimation::GetDuration(int sequence) const { return d->Duration(sequence); }

float StudioAnimation::GetGroundSpeed() const
{
    Vector vecMove;
    QAngle vecAngles;
    Studio_SeqMovement(&d->hdr, d->sequence, 0, 1, d->poseparameter, vecMove, vecAngles);
    const float t = d->Duration(d->sequence);
    return t > 0 ? vecMove.Length() / t : 0;
}

void StudioAnimation::SetBlendTime(float seconds)
{
    if (seconds > 0.0f)
        d->blendtime = seconds;
}

void StudioAnimation::StartBlending()
{
    // Switch back to old sequence (this will oscillate between this one and the last one)
    SetSequence(d->prevsequence);
}

float StudioAnimation::GetTransitionAmount() const
{
    if (blendSequenceChanges && d->sequencetime < d->blendtime && d->prevsequence != d->sequence)
        return d->sequencetime / d->blendtime;
    return 0.0f;
}

// upstream StudioModel::SetUpBones without the viewer-only parts: bones stay in model space, and physics
// preview, merged models and head turn are not ported. Jiggle bones simulate in `modelToView` space, as upstream
// does through g_viewtransform, so orbiting or panning the camera swings them like moving the model.
void StudioAnimation::SetUpBones(std::vector<QMatrix4x4> &boneToModel, const QMatrix4x4 &modelToView)
{
    CStudioHdr *pStudioHdr = &d->hdr;
    const mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc(d->sequence);
    // The viewer draws bones, attachments and hitboxes at any time, so every bone is set up.
    const BoneFlags boneMask = BoneFlags::UsedByAnything;
    auto *pos = d->pos;
    auto *q = d->q;

    const QAngle a1(0, 0, 0);
    const Vector p1(0, 0, 0);
    CIKContext *pIK = nullptr;
    d->ik.Init(pStudioHdr, a1, p1, d->realtime, d->framecounter++, boneMask);
    if (enableIK)
        pIK = &d->ik;

    IBoneSetup boneSetup(pStudioHdr, boneMask, d->poseparameter);
    boneSetup.InitPose(pos, q);
    boneSetup.AccumulatePose(pos, q, d->sequence, d->cycle, 1.0, d->realtime, pIK);

    if (blendSequenceChanges && d->sequencetime < d->blendtime && d->prevsequence != d->sequence &&
        d->prevsequence < pStudioHdr->GetNumSeq() && !HasFlag(seqdesc.flags, StudioSequenceFlags::Snap))
    {
        // Make sure frame is valid
        if (d->prevcycle >= 1.0)
            d->prevcycle = 0.0f;
        float s = 1.0 - (d->sequencetime / d->blendtime);
        s = 3 * s * s - 2 * s * s * s;
        boneSetup.AccumulatePose(pos, q, d->prevsequence, d->prevcycle, s, d->realtime, nullptr);
    }
    else
        d->prevcycle = d->cycle;

    int iMaxPriority = 0;
    for (const auto &layer : d->layers)
        if (layer.weight > 0)
            iMaxPriority = std::max(layer.priority, iMaxPriority);
    for (int j = 0; j <= iMaxPriority; j++)
        for (const auto &layer : d->layers)
            if (layer.priority == j && layer.weight > 0)
                boneSetup.AccumulatePose(pos, q, layer.sequence, layer.cycle, layer.weight, d->realtime, pIK);

    CIKContext auto_ik;
    auto_ik.Init(pStudioHdr, a1, p1, 0.0, 0, boneMask);
    boneSetup.CalcAutoplaySequences(pos, q, d->autoplaytime, &auto_ik);
    boneSetup.CalcBoneAdj(pos, q, d->controller);

    CBoneBitList boneComputed;
    if (pIK)
    {
        Vector deltaPos;
        QAngle deltaAngles;
        d->GetMovement(d->prevIKCycles, deltaPos, deltaAngles);
        pIK->UpdateTargets(pos, q, d->boneToWorld, boneComputed);
        for (int i = 0; i < pIK->m_target.size(); i++)
        {
            CIKTarget *pTarget = &pIK->m_target[i];
            if (pTarget->type == IkRuleType::Ground)
            {
                // hack in movement; with an identity view transform the target drops straight onto its floor
                pTarget->est.pos -= deltaPos;
                pTarget->est.pos.z = pTarget->est.floor;
                pTarget->est.q.Init(0, 0, 0, 1);
            }
        }
        pIK->SolveDependencies(pos, q, d->boneToWorld, boneComputed);
    }

    const mstudiobone_t *pbones = pStudioHdr->pBone(0);
    CBoneAccessor bones(d->boneToWorld);
    ALIGN16 matrix3x4a_t bonematrix;
    for (int i = 0; i < pStudioHdr->numbones(); i++)
    {
        if (!HasFlag(pStudioHdr->boneFlags(i), boneMask))
        {
            // upstream writes NaN here; the viewer keeps the bone at its default pose so overlays stay drawable
            QuaternionMatrix(pbones[i].quat, pbones[i].pos, bonematrix);
            if (pbones[i].parent == -1)
                d->boneToWorld[i] = bonematrix;
            else
                ConcatTransforms(d->boneToWorld[pbones[i].parent], bonematrix, d->boneToWorld[i]);
            continue;
        }
        if (boneComputed.IsBoneMarked(i))
            continue; // already calculated by IK
        if (CalcProceduralBone(pStudioHdr, i, bones))
            continue;
        QuaternionMatrix(q[i], bonematrix);
        bonematrix[0][3] = pos[i][0];
        bonematrix[1][3] = pos[i][1];
        bonematrix[2][3] = pos[i][2];
        if (HasFlag(pbones[i].flags, BoneFlags::AlwaysProcedural) && pbones[i].proctype == ProcBoneType::Jiggle)
        {
            // Physics-based "jiggle" bone: driven from the goal orientation the animation wants.
            matrix3x4a_t goalMX;
            if (pbones[i].parent == -1)
                goalMX = bonematrix;
            else
                ConcatTransforms(d->boneToWorld[pbones[i].parent], bonematrix, goalMX);
            if (d->realtime != d->jiggleTime)
            {
                const auto *jiggleInfo = static_cast<const mstudiojigglebone_t *>(pbones[i].pProcedure());
                const matrix3x4_t toView = FromQt(modelToView), toModel = FromQt(modelToView.inverted());
                matrix3x4a_t goalView, jiggledView, jiggled, goalInverse;
                ConcatTransforms(toView, goalMX, goalView);
                d->jiggle.BuildJiggleTransformations(i, d->realtime, jiggleInfo, goalView, jiggledView, false);
                ConcatTransforms(toModel, jiggledView, jiggled);
                MatrixInvert(goalMX, goalInverse);
                ConcatTransforms(goalInverse, jiggled, d->jiggleOffset[i]);
            }
            // Between steps (camera drags, frame scrubbing while paused) keep the last offset on the current goal.
            ConcatTransforms(goalMX, d->jiggleOffset[i], d->boneToWorld[i]);
        }
        else if (pbones[i].parent == -1)
            d->boneToWorld[i] = bonematrix;
        else
            ConcatTransforms(d->boneToWorld[pbones[i].parent], bonematrix, d->boneToWorld[i]);

        if (HasFlag(pbones[i].flags, BoneFlags::WorldAlign))
        {
            const Vector vecOrigin = d->boneToWorld[i].GetOrigin();
            d->boneToWorld[i].Init(Vector(1, 0, 0), Vector(0, 1, 0), Vector(0, 0, 1), vecOrigin);
        }
    }
    d->jiggleTime = d->realtime;

    boneToModel.resize(pStudioHdr->numbones());
    for (int i = 0; i < pStudioHdr->numbones(); i++)
        ToQt(d->boneToWorld[i], boneToModel[i]);
}
