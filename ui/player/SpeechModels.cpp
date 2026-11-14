#include "SpeechModels.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>

SpeechModels::SpeechModels(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    m_directory = qEnvironmentVariable("TVE_MODEL_DIR");
    if (m_directory.isEmpty())
        m_directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Models");
    m_baseUrl = qEnvironmentVariable("TVE_MODEL_BASE_URL");
    if (!m_baseUrl.isEmpty() && !m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl += QLatin1Char('/');
}

SpeechModels::~SpeechModels()
{
    if (m_reply)
        m_reply->abort();
}

const std::vector<SpeechModels::Model>& SpeechModels::catalog()
{
    // Sizes and SHA-256 from the repository's file list (its Git LFS records).
    static const std::vector<Model> models{
        {QStringLiteral("base.en-q5_1"), QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "Base · English")), QStringLiteral("ggml-base.en-q5_1.bin"), 59721011,
         QStringLiteral("4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f"), true,
         QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "fast"))},
        {QStringLiteral("base-q5_1"), QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "Base · 99 languages")), QStringLiteral("ggml-base-q5_1.bin"), 59707625,
         QStringLiteral("422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898"), false,
         QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "fast"))},
        {QStringLiteral("small.en-q5_1"), QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "Small · English")), QStringLiteral("ggml-small.en-q5_1.bin"),
         190098681, QStringLiteral("bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30"), true,
         QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "more accurate"))},
        {QStringLiteral("small-q5_1"), QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "Small · 99 languages")), QStringLiteral("ggml-small-q5_1.bin"),
         190085487, QStringLiteral("ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb"), false,
         QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "more accurate"))},
        {QStringLiteral("large-v3-turbo-q5_0"), QStringLiteral("Large v3 Turbo"),
         QStringLiteral("ggml-large-v3-turbo-q5_0.bin"), 574041195,
         QStringLiteral("394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2"), false,
         QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "99 languages · most accurate"))},
        // Speech enhancement: the ONNX export in the DeepFilterNet repository (models/).
        {QStringLiteral("deepfilternet3"), QStringLiteral("DeepFilterNet3"), QStringLiteral("DeepFilterNet3_onnx.tar.gz"),
         7983136, QStringLiteral("c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616"), false,
         QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "speech enhancement · 48 kHz")), QStringLiteral("enhance"),
         QStringLiteral("https://github.com/Rikorose/DeepFilterNet/raw/main/models/DeepFilterNet3_onnx.tar.gz"),
         {QStringLiteral("enc.onnx"), QStringLiteral("erb_dec.onnx"), QStringLiteral("df_dec.onnx"),
          QStringLiteral("config.ini")}},
        // Depth: Apple's Core ML conversion of Depth Anything V2 Small, at a fixed revision
        // (sizes and SHA-256 from its file list; the manifest's measured).
        {QStringLiteral("depth-anything-v2-small-f16"), QStringLiteral("Depth Anything V2 Small"),
         QStringLiteral("DepthAnythingV2SmallF16.mlpackage"), 617 + 399433 + 49419072, QString(), false,
         QStringLiteral(QT_TRANSLATE_NOOP("SpeechModels", "depth of field")), QStringLiteral("depth"),
         QStringLiteral("https://huggingface.co/apple/coreml-depth-anything-v2-small/resolve/"
                        "cfef6f6f2a70783dedc0bfae40cecbc2052285d3/DepthAnythingV2SmallF16.mlpackage/"),
         {},
         {{QStringLiteral("Manifest.json"), 617, QStringLiteral("2883ae290c48fe916dc5ececac03a7d847fa277165a49ef5652fa1d2b9cb55f7")},
          {QStringLiteral("Data/com.apple.CoreML/model.mlmodel"), 399433,
           QStringLiteral("44ac97a3efcfd52113183fb2862ff59cd0368e9ec2e30a90a54980dd11407042")},
          {QStringLiteral("Data/com.apple.CoreML/weights/weight.bin"), 49419072,
           QStringLiteral("fa60d9b6a155734f59029ebb882fd54e549bfaee3539c1a9cbd2cbbab64a0fed")}}},
    };
    return models;
}

const SpeechModels::Model* SpeechModels::find(const QString& id)
{
    for (const Model& model : catalog())
        if (model.id == id)
            return &model;
    return nullptr;
}

std::optional<QString> SpeechModels::path(const QString& id) const
{
    const Model* model = find(id);
    if (!model)
        return std::nullopt;
    if (!model->parts.isEmpty()) {
        const QDir folder(QDir(m_directory).filePath(model->id + QLatin1Char('/') + model->file));
        for (const Model::Part& part : model->parts)
            if (QFileInfo(folder.filePath(part.path)).size() != part.size)
                return std::nullopt;
        return folder.path();
    }
    if (!model->contents.isEmpty()) {
        const QDir folder(QDir(m_directory).filePath(model->id));
        for (const QString& name : model->contents)
            if (!QFileInfo(folder.filePath(name)).isFile())
                return std::nullopt;
        return folder.path();
    }
    const QString file = QDir(m_directory).filePath(model->file);
    const QFileInfo info(file);
    if (!info.isFile() || info.size() != model->size)
        return std::nullopt;
    return file;
}

QVariantList SpeechModels::list(const QString& kind) const
{
    QVariantList list;
    for (const Model& model : catalog()) {
        if (model.kind != kind)
            continue;
        list << QVariantMap{{QStringLiteral("id"), model.id},
                            {QStringLiteral("title"), tr(model.title.toUtf8().constData())},
                            {QStringLiteral("size"), double(model.size)},
                            {QStringLiteral("englishOnly"), model.englishOnly},
                            {QStringLiteral("note"), tr(model.note.toUtf8().constData())},
                            {QStringLiteral("downloaded"), path(model.id).has_value()}};
    }
    return list;
}

QVariantMap SpeechModels::progress() const
{
    if (m_downloading.isEmpty())
        return {};
    const Model* model = find(m_downloading);
    const double total = model ? double(model->size) : 0.0;
    const double received = double(m_partsBefore + m_received);
    return {{QStringLiteral("id"), m_downloading},
            {QStringLiteral("received"), received},
            {QStringLiteral("total"), total},
            {QStringLiteral("progress"), total > 0 ? received / total : 0.0}};
}

bool SpeechModels::download(const QString& id, QString* error)
{
    const Model* model = find(id);
    if (!model) {
        *error = tr("Unknown speech model: %1").arg(id);
        return false;
    }
    if (!m_downloading.isEmpty()) {
        *error = tr("A model is already downloading");
        return false;
    }
    if (path(id)) {
        emit finished(id, true, {});
        return true;
    }
    QDir().mkpath(m_directory);
    if (!model->parts.isEmpty()) {
        // Into <id>.part/<file>/, then moved to <id>/ once every part is in.
        QDir(QDir(m_directory).filePath(model->id + QStringLiteral(".part"))).removeRecursively();
        m_downloading = id;
        m_partsBefore = 0;
        m_cancelled = false;
        fetchPart(*model, 0);
        emit changed();
        return true;
    }
    m_file = std::make_unique<QFile>(QDir(m_directory).filePath(model->file + QStringLiteral(".part")));
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = tr("Cannot write to %1").arg(m_directory);
        m_file.reset();
        return false;
    }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_downloading = id;
    m_received = 0;
    m_cancelled = false;
    const QString url = !m_baseUrl.isEmpty() ? m_baseUrl + model->file
                      : !model->url.isEmpty() ? model->url
                                              : QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/") + model->file;
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray data = m_reply->readAll();
        m_file->write(data);
        m_hash->addData(data);
        m_received += data.size();
        emit changed();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, model, size = model->size, sha = model->sha256] {
        const QByteArray rest = m_reply->readAll();
        m_file->write(rest);
        m_hash->addData(rest);
        m_received += rest.size();
        m_file->close();
        if (m_cancelled)
            return end(false, {});
        if (m_reply->error() != QNetworkReply::NoError)
            return end(false, tr("Download failed: %1").arg(m_reply->errorString()));
        if (m_received != size || QString::fromLatin1(m_hash->result().toHex()) != sha)
            return end(false, tr("The downloaded model is not the expected file (size or SHA-256 differs)"));
        const QString finalPath = m_file->fileName().chopped(5);
        if (!model->contents.isEmpty()) {
            const QString why = unpack(*model, m_file->fileName());
            return end(why.isEmpty(), why);
        }
        QFile::remove(finalPath);
        if (!QFile::rename(m_file->fileName(), finalPath))
            return end(false, tr("Cannot keep the downloaded model in %1").arg(m_directory));
        end(true, {});
    });
    emit changed();
    return true;
}

void SpeechModels::fetchPart(const Model& model, int index)
{
    const Model::Part& part = model.parts[index];
    const QDir scratch(QDir(m_directory).filePath(model.id + QStringLiteral(".part/") + model.file));
    const QString target = scratch.filePath(part.path);
    QDir().mkpath(QFileInfo(target).path());
    m_file = std::make_unique<QFile>(target);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate))
        return end(false, tr("Cannot write to %1").arg(m_directory));
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_received = 0;
    const QString url = !m_baseUrl.isEmpty() ? m_baseUrl + model.id + QLatin1Char('/') + part.path : model.url + part.path;
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray data = m_reply->readAll();
        m_file->write(data);
        m_hash->addData(data);
        m_received += data.size();
        emit changed();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, &model, index] {
        const QByteArray rest = m_reply->readAll();
        m_file->write(rest);
        m_hash->addData(rest);
        m_received += rest.size();
        m_file->close();
        const Model::Part& part = model.parts[index];
        const QString scratch = QDir(m_directory).filePath(model.id + QStringLiteral(".part"));
        const auto fail = [&](const QString& why) {
            m_file.reset();
            QDir(scratch).removeRecursively();
            end(false, why);
        };
        if (m_cancelled)
            return fail({});
        if (m_reply->error() != QNetworkReply::NoError)
            return fail(tr("Download failed: %1").arg(m_reply->errorString()));
        if (m_received != part.size || QString::fromLatin1(m_hash->result().toHex()) != part.sha256)
            return fail(tr("The downloaded model is not the expected file (size or SHA-256 differs)"));
        m_reply->deleteLater();
        m_reply = nullptr;
        m_partsBefore += m_received;
        if (index + 1 < model.parts.size())
            return fetchPart(model, index + 1);
        // All in: into place.
        m_file.reset();
        const QString target = QDir(m_directory).filePath(model.id);
        QDir(target).removeRecursively();
        if (!QDir().rename(scratch, target))
            return fail(tr("Cannot keep the downloaded model in %1").arg(m_directory));
        m_partsBefore = 0;
        end(true, {});
    });
}

void SpeechModels::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort();
}

QString SpeechModels::unpack(const Model& model, const QString& archive)
{
    // The archive's SHA-256 matched, so what is in it is known; the system's tar unpacks it
    // into a scratch folder, and the files the model needs move into <id>/.
    QDir models(m_directory);
    const QString scratch = models.filePath(model.id + QStringLiteral(".unpack"));
    QDir(scratch).removeRecursively();
    models.mkpath(scratch);
    QProcess tar;
    tar.start(QStringLiteral("/usr/bin/tar"), {QStringLiteral("-xzf"), archive, QStringLiteral("-C"), scratch});
    const bool unpacked = tar.waitForFinished(60000) && tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0;
    QFile::remove(archive);
    const auto cleanUp = qScopeGuard([&] { QDir(scratch).removeRecursively(); });
    if (!unpacked)
        return tr("The downloaded model could not be unpacked");
    const QString target = models.filePath(model.id);
    QDir(target).removeRecursively();
    models.mkpath(target);
    for (const QString& name : model.contents) {
        // Wherever it is in the archive (DeepFilterNet's is under tmp/export/).
        QString found;
        QList<QDir> folders{QDir(scratch)};
        while (!folders.isEmpty() && found.isEmpty()) {
            const QDir folder = folders.takeFirst();
            if (QFileInfo(folder.filePath(name)).isFile())
                found = folder.filePath(name);
            for (const QString& sub : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                folders << QDir(folder.filePath(sub));
        }
        if (found.isEmpty() || !QFile::rename(found, QDir(target).filePath(name))) {
            QDir(target).removeRecursively();
            return tr("The downloaded model is missing %1").arg(name);
        }
    }
    return {};
}

void SpeechModels::end(bool ok, const QString& error)
{
    const QString id = m_downloading;
    if (!ok && m_file)
        QFile::remove(m_file->fileName());
    m_file.reset();
    m_hash.reset();
    if (m_reply)
        m_reply->deleteLater();
    m_downloading.clear();
    m_received = 0;
    m_partsBefore = 0;
    emit changed();
    emit finished(id, ok, error);
}
