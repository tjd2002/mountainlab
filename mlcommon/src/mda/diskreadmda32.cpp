#include "diskreadmda32.h"
#include <stdio.h>
#include "mdaio.h"
#include <math.h>
#include <QFile>
#include <QCryptographicHash>
#include <QDir>
#include "cachemanager.h"
#include "taskprogress.h"
#ifdef USE_REMOTE_READ_MDA
#include "remotereadmda.h"
#endif

#define MAX_PATH_LEN 10000
#define DEFAULT_CHUNK_SIZE 1e6

/// TODO (LOW) make tmp directory with different name on server, so we can really test if it is doing the computation in the right place

class DiskReadMda32Private {
public:
    DiskReadMda32* q;
    FILE* m_file;
    bool m_file_open_failed;
    bool m_header_read;
    MDAIO_HEADER m_header;
    bool m_reshaped;
    long m_mda_header_total_size;
    Mda32 m_internal_chunk;
    long m_current_internal_chunk_index;
    Mda32 m_memory_mda;
    bool m_use_memory_mda;

#ifdef USE_REMOTE_READ_MDA
    bool m_use_remote_mda;
    RemoteReadMda m_remote_mda;
#endif

    QString m_path;
    void construct_and_clear();
    bool read_header_if_needed();
    bool open_file_if_needed();
    void copy_from(const DiskReadMda32& other);
    long total_size();
};

DiskReadMda32::DiskReadMda32(const QString& path)
{
    d = new DiskReadMda32Private;
    d->q = this;
    d->construct_and_clear();
    if (!path.isEmpty()) {
        this->setPath(path);
    }
}

DiskReadMda32::DiskReadMda32(const DiskReadMda32& other)
{
    d = new DiskReadMda32Private;
    d->q = this;
    d->construct_and_clear();
    d->copy_from(other);
}

DiskReadMda32::DiskReadMda32(const Mda32& X)
{
    d = new DiskReadMda32Private;
    d->q = this;
    d->construct_and_clear();
    d->m_use_memory_mda = true;
    d->m_memory_mda = X;
}

DiskReadMda32::~DiskReadMda32()
{
    if (d->m_file) {
        fclose(d->m_file);
    }
    delete d;
}

void DiskReadMda32::operator=(const DiskReadMda32& other)
{
    d->copy_from(other);
}

void DiskReadMda32::setPath(const QString& file_path)
{
    if (d->m_file) {
        fclose(d->m_file);
        d->m_file = 0;
    }
    d->construct_and_clear();

    if (file_path.startsWith("http://")) {
#ifdef USE_REMOTE_READ_MDA
        d->m_use_remote_mda = true;
        d->m_remote_mda.setPath(file_path);
#endif
    }
    else if ((file_path.endsWith(".txt")) || (file_path.endsWith(".csv"))) {
        Mda32 X(file_path);
        (*this) = X;
        return;
    }
    else {
        d->m_path = file_path;
    }
}

void DiskReadMda32::setRemoteDataType(const QString& dtype)
{
#ifdef USE_REMOTE_READ_MDA
    d->m_remote_mda.setRemoteDataType(dtype);
#else
    Q_UNUSED(dtype)
#endif
}

void DiskReadMda32::setDownloadChunkSize(long size)
{
#ifdef USE_REMOTE_READ_MDA
    d->m_remote_mda.setDownloadChunkSize(size);
#else
    Q_UNUSED(size)
#endif
}

long DiskReadMda32::downloadChunkSize()
{
#ifdef USE_REMOTE_READ_MDA
    return d->m_remote_mda.downloadChunkSize();
#else
    return 0;
#endif
}

QString compute_memory_checksum32(long nbytes, void* ptr)
{
    QByteArray X((char*)ptr, nbytes);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(X);
    return QString(hash.result().toHex());
}

QString compute_mda_checksum(Mda32& X)
{
    QString ret = compute_memory_checksum32(X.totalSize() * sizeof(dtype32), X.dataPtr());
    ret += "-";
    for (int i = 0; i < X.ndims(); i++) {
        if (i > 0)
            ret += "x";
        ret += QString("%1").arg(X.size(i));
    }
    return ret;
}

QString DiskReadMda32::makePath() const
{
    if (d->m_use_memory_mda) {
        QString checksum = compute_mda_checksum(d->m_memory_mda);
        QString fname = CacheManager::globalInstance()->makeLocalFile(checksum + ".makePath.mda", CacheManager::ShortTerm);
        if (QFile::exists(fname))
            return fname;
        if (d->m_memory_mda.write32(fname + ".tmp")) {
            if (QFile::rename(fname + ".tmp", fname)) {
                return fname;
            }
        }
        QFile::remove(fname);
        QFile::remove(fname + ".tmp");
    }
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda) {
        return d->m_remote_mda.makePath();
    }
#endif
    return d->m_path;
}

long DiskReadMda32::N1() const
{
    if (d->m_use_memory_mda) {
        return d->m_memory_mda.N1();
    }
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda) {
        return d->m_remote_mda.N1();
    }
#endif
    if (!d->read_header_if_needed()) {
        return 0;
    }
    return d->m_header.dims[0];
}

long DiskReadMda32::N2() const
{
    if (d->m_use_memory_mda)
        return d->m_memory_mda.N2();
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda)
        return d->m_remote_mda.N2();
#endif
    if (!d->read_header_if_needed())
        return 0;
    return d->m_header.dims[1];
}

long DiskReadMda32::N3() const
{
    if (d->m_use_memory_mda)
        return d->m_memory_mda.N3();
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda)
        return d->m_remote_mda.N3();
#endif
    if (!d->read_header_if_needed())
        return 0;
    return d->m_header.dims[2];
}

long DiskReadMda32::N4() const
{
    if (d->m_use_memory_mda)
        return d->m_memory_mda.N4();
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda)
        return 1;
#endif
    if (!d->read_header_if_needed())
        return 0;
    return d->m_header.dims[3];
}

long DiskReadMda32::N5() const
{
    if (d->m_use_memory_mda)
        return d->m_memory_mda.N5();
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda)
        return 1;
#endif
    if (!d->read_header_if_needed())
        return 0;
    return d->m_header.dims[4];
}

long DiskReadMda32::N6() const
{
    if (d->m_use_memory_mda)
        return d->m_memory_mda.N6();
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda)
        return 1;
#endif
    if (!d->read_header_if_needed())
        return 0;
    return d->m_header.dims[5];
}

long DiskReadMda32::totalSize() const
{
    return d->total_size();
}

bool DiskReadMda32::reshape(long N1b, long N2b, long N3b, long N4b, long N5b, long N6b)
{
    long size_b = N1b * N2b * N3b * N4b * N5b * N6b;
    if (size_b != this->totalSize()) {
        qWarning() << "Cannot reshape because sizes do not match" << this->totalSize() << N1b << N2b << N3b << N4b << N5b << N6b;
        return false;
    }
    if (d->m_use_memory_mda) {
        if (d->m_memory_mda.reshape(N1b, N2b, N3b, N4b, N5b, N6b)) {
            d->m_reshaped = true;
            return true;
        }
        else
            return false;
    }
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda) {
        if ((N4b != 1) || (N5b != 1) || (N6b != 1)) {
            qWarning() << "Cannot reshape... remote mda can only have 3 dimensions, at this time.";
            return false;
        }
        if (d->m_remote_mda.reshape(N1b, N2b, N3b)) {
            d->m_reshaped = true;
            return true;
        }
        else
            return false;
    }
#endif
    if (!d->read_header_if_needed()) {
        return false;
    }
    d->m_header.dims[0] = N1b;
    d->m_header.dims[1] = N2b;
    d->m_header.dims[2] = N3b;
    d->m_header.dims[3] = N4b;
    d->m_header.dims[4] = N5b;
    d->m_header.dims[5] = N6b;
    d->m_reshaped = true;
    return true;
}

DiskReadMda32 DiskReadMda32::reshaped(long N1b, long N2b, long N3b, long N4b, long N5b, long N6b)
{
    long size_b = N1b * N2b * N3b * N4b * N5b * N6b;
    if (size_b != this->totalSize()) {
        qWarning() << "Cannot reshape because sizes do not match" << this->totalSize() << N1b << N2b << N3b << N4b << N5b << N6b;
        return (*this);
    }
    DiskReadMda32 ret = (*this);
    ret.reshape(N1b, N2b, N3b, N4b, N5b, N6b);
    return ret;
}

bool DiskReadMda32::readChunk(Mda32& X, long i, long size) const
{
    if (d->m_use_memory_mda) {
        d->m_memory_mda.getChunk(X, i, size);
        return true;
    }
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda) {
        return d->m_remote_mda.readChunk32(X, i, size);
    }
#endif
    if (!d->open_file_if_needed())
        return false;
    X.allocate(size, 1);
    long jA = qMax(i, 0L);
    long jB = qMin(i + size - 1, d->total_size() - 1);
    long size_to_read = jB - jA + 1;
    if (size_to_read > 0) {
        fseek(d->m_file, d->m_header.header_size + d->m_header.num_bytes_per_entry * (jA), SEEK_SET);
        long bytes_read = mda_read_float32(&X.dataPtr()[jA - i], &d->m_header, size_to_read, d->m_file);
        TaskManager::TaskProgressMonitor::globalInstance()->incrementQuantity("bytes_read", bytes_read);
        if (bytes_read != size_to_read) {
            printf("Warning problem reading chunk in diskreadmda: %ld<>%ld\n", bytes_read, size_to_read);
            return false;
        }
    }
    return true;
}

bool DiskReadMda32::readChunk(Mda32& X, long i1, long i2, long size1, long size2) const
{
    if (size2 == 0) {
        return readChunk(X, i1, size1);
    }
    if (d->m_use_memory_mda) {
        d->m_memory_mda.getChunk(X, i1, i2, size1, size2);
        return true;
    }
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda) {
        if ((size1 != N1()) || (i1 != 0)) {
            qWarning() << "Cannot handle this case yet.";
            return false;
        }
        Mda32 tmp;
        if (!d->m_remote_mda.readChunk32(tmp, i2 * size1, size1 * size2))
            return false;
        X.allocate(size1, size2);
        dtype32* Xptr = X.dataPtr();
        dtype32* tmp_ptr = tmp.dataPtr();
        for (long i = 0; i < size1 * size2; i++) {
            Xptr[i] = tmp_ptr[i];
        }
        return true;
    }
#endif
    if (!d->open_file_if_needed())
        return false;
    if ((size1 == N1()) && (i1 == 0)) {
        //easy case
        X.allocate(size1, size2);
        long jA = qMax(i2, 0L);
        long jB = qMin(i2 + size2 - 1, N2() - 1);
        long size2_to_read = jB - jA + 1;
        if (size2_to_read > 0) {
            fseek(d->m_file, d->m_header.header_size + d->m_header.num_bytes_per_entry * (i1 + N1() * jA), SEEK_SET);
            long bytes_read = mda_read_float32(&X.dataPtr()[(jA - i2) * size1], &d->m_header, size1 * size2_to_read, d->m_file);
            TaskManager::TaskProgressMonitor::globalInstance()->incrementQuantity("bytes_read", bytes_read);
            if (bytes_read != size1 * size2_to_read) {
                printf("Warning problem reading 2d chunk in diskreadmda: %ld<>%ld\n", bytes_read, size1 * size2);
                return false;
            }
        }
        return true;
    }
    else {
        printf("Warning: This case not yet supported (diskreadmda::readchunk 2d).\n");
        return false;
    }
}

bool DiskReadMda32::readChunk(Mda32& X, long i1, long i2, long i3, long size1, long size2, long size3) const
{
    if (size3 == 0) {
        if (size2 == 0) {
            return readChunk(X, i1, size1);
        }
        else {
            return readChunk(X, i1, i2, size1, size2);
        }
    }
    if (d->m_use_memory_mda) {
        d->m_memory_mda.getChunk(X, i1, i2, i3, size1, size2, size3);
        return true;
    }
#ifdef USE_REMOTE_READ_MDA
    if (d->m_use_remote_mda) {
        if ((size1 != N1()) || (i1 != 0) || (size2 != N2()) || (i2 != 0)) {
            qWarning() << "Cannot handle this case yet **." << size1 << N1() << i1 << size2 << N2() << i2 << this->d->m_path;
            return false;
        }

        Mda32 tmp;
        if (!d->m_remote_mda.readChunk32(tmp, i3 * size1 * size2, size1 * size2 * size3))
            return false;
        X.allocate(size1, size2, size3);
        dtype32* Xptr = X.dataPtr();
        dtype32* tmp_ptr = tmp.dataPtr();
        for (long i = 0; i < size1 * size2 * size3; i++) {
            Xptr[i] = tmp_ptr[i];
        }
        return true;
    }
#endif
    if (!d->open_file_if_needed())
        return false;
    if ((size1 == N1()) && (size2 == N2())) {
        //easy case
        X.allocate(size1, size2, size3);
        long jA = qMax(i3, 0L);
        long jB = qMin(i3 + size3 - 1, N3() - 1);
        long size3_to_read = jB - jA + 1;
        if (size3_to_read > 0) {
            fseek(d->m_file, d->m_header.header_size + d->m_header.num_bytes_per_entry * (i1 + N1() * i2 + N1() * N2() * jA), SEEK_SET);
            long bytes_read = mda_read_float32(&X.dataPtr()[(jA - i3) * size1 * size2], &d->m_header, size1 * size2 * size3_to_read, d->m_file);
            TaskManager::TaskProgressMonitor::globalInstance()->incrementQuantity("bytes_read", bytes_read);
            if (bytes_read != size1 * size2 * size3_to_read) {
                printf("Warning problem reading 3d chunk in diskreadmda: %ld<>%ld\n", bytes_read, size1 * size2 * size3_to_read);
                return false;
            }
        }
        return true;
    }
    else {
        printf("Warning: This case not yet supported (diskreadmda::readchunk 3d).\n");
        return false;
    }
}

dtype32 DiskReadMda32::value(long i) const
{
    if (d->m_use_memory_mda)
        return d->m_memory_mda.value(i);
    if ((i < 0) || (i >= d->total_size()))
        return 0;
    long chunk_index = i / DEFAULT_CHUNK_SIZE;
    long offset = i - DEFAULT_CHUNK_SIZE * chunk_index;
    if (d->m_current_internal_chunk_index != chunk_index) {
        long size_to_read = DEFAULT_CHUNK_SIZE;
        if (chunk_index * DEFAULT_CHUNK_SIZE + size_to_read > d->total_size())
            size_to_read = d->total_size() - chunk_index * DEFAULT_CHUNK_SIZE;
        if (size_to_read) {
            this->readChunk(d->m_internal_chunk, chunk_index * DEFAULT_CHUNK_SIZE, size_to_read);
        }
        d->m_current_internal_chunk_index = chunk_index;
    }
    return d->m_internal_chunk.value(offset);
}

dtype32 DiskReadMda32::value(long i1, long i2) const
{
    if (d->m_use_memory_mda)
        return d->m_memory_mda.value(i1, i2);
    if ((i1 < 0) || (i1 >= N1()))
        return 0;
    if ((i2 < 0) || (i2 >= N2()))
        return 0;
    return value(i1 + N1() * i2);
}

dtype32 DiskReadMda32::value(long i1, long i2, long i3) const
{
    if (d->m_use_memory_mda)
        return d->m_memory_mda.value(i1, i2, i3);
    if ((i1 < 0) || (i1 >= N1()))
        return 0;
    if ((i2 < 0) || (i2 >= N2()))
        return 0;
    if ((i3 < 0) || (i3 >= N3()))
        return 0;
    return value(i1 + N1() * i2 + N1() * N2() * i3);
}

void DiskReadMda32Private::construct_and_clear()
{
    m_file_open_failed = false;
    m_file = 0;
    m_current_internal_chunk_index = -1;
    m_use_memory_mda = false;
    m_header_read = false;
    m_reshaped = false;
#ifdef USE_REMOTE_READ_MDA
    m_use_remote_mda = false;
#endif
    this->m_internal_chunk = Mda32();
    this->m_mda_header_total_size = 0;
    this->m_memory_mda = Mda32();
    this->m_path = "";
#ifdef USE_REMOTE_READ_MDA
    this->m_remote_mda = RemoteReadMda();
#endif
}

bool DiskReadMda32Private::read_header_if_needed()
{
    if (m_header_read)
        return true;
#ifdef USE_REMOTE_READ_MDA
    if (m_use_remote_mda) {
        m_header_read = true;
        return true;
    }
#endif
    if (m_use_memory_mda) {
        m_header_read = true;
        return true;
    }
    bool file_was_open = (m_file != 0); //so we can restore to previous state (we don't want too many files open unnecessarily)
    if (!open_file_if_needed()) //if successful, it will read the header
        return false;
    if (!m_file)
        return false; //should never happen
    if (!file_was_open) {
        fclose(m_file);
        m_file = 0;
    }
    return true;
}

bool DiskReadMda32Private::open_file_if_needed()
{
#ifdef USE_REMOTE_READ_MDA
    if (m_use_remote_mda)
        return true;
#endif
    if (m_use_memory_mda)
        return true;
    if (m_file)
        return true;
    if (m_file_open_failed)
        return false;
    if (m_path.isEmpty())
        return false;
    m_file = fopen(m_path.toLatin1().data(), "rb");
    if (m_file) {
        if (!m_header_read) {
            //important not to read it again in case we have reshaped the array
            mda_read_header(&m_header, m_file);
            m_mda_header_total_size = 1;
            for (int i = 0; i < MDAIO_MAX_DIMS; i++)
                m_mda_header_total_size *= m_header.dims[i];
            m_header_read = true;
        }
    }
    else {
        printf("Failed to open diskreadmda32 file: %s\n", m_path.toLatin1().data());
        m_file_open_failed = true; //we don't want to try this more than once
        return false;
    }
    return true;
}

void DiskReadMda32Private::copy_from(const DiskReadMda32& other)
{
    /// TODO (LOW) think about copying over additional information such as internal chunks

    if (this->m_file) {
        fclose(this->m_file);
        this->m_file = 0;
    }
    this->construct_and_clear();
    this->m_current_internal_chunk_index = -1;
    this->m_file_open_failed = other.d->m_file_open_failed;
    this->m_header = other.d->m_header;
    this->m_header_read = other.d->m_header_read;
    this->m_mda_header_total_size = other.d->m_mda_header_total_size;
    this->m_memory_mda = other.d->m_memory_mda;
    this->m_path = other.d->m_path;
#ifdef USE_REMOTE_READ_MDA
    this->m_remote_mda = other.d->m_remote_mda;
#endif
    this->m_reshaped = other.d->m_reshaped;
    this->m_use_memory_mda = other.d->m_use_memory_mda;
#ifdef USE_REMOTE_READ_MDA
    this->m_use_remote_mda = other.d->m_use_remote_mda;
#endif
}

long DiskReadMda32Private::total_size()
{
    if (m_use_memory_mda)
        return m_memory_mda.totalSize();
#ifdef USE_REMOTE_READ_MDA
    if (m_use_remote_mda)
        return m_remote_mda.N1() * m_remote_mda.N2() * m_remote_mda.N3();
#endif
    if (!read_header_if_needed())
        return 0;
    return m_mda_header_total_size;
}