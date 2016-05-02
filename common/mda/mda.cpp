#include "mda.h"
#include "mdaio.h"
#include <stdio.h>

#define MDA_MAX_DIMS 6

class MdaPrivate
{
public:
	Mda *q;
	double *m_data;
	long m_dims[MDA_MAX_DIMS];
	long m_total_size;

	void do_construct();
	void copy_from(const Mda &other);
	int determine_num_dims(long N1,long N2,long N3,long N4,long N5,long N6);
	bool safe_index(long i);
	bool safe_index(long i1,long i2);
	bool safe_index(long i1,long i2,long i3);
	bool safe_index(long i1,long i2,long i3,long i4,long i5,long i6);
};

Mda::Mda(long N1, long N2, long N3, long N4, long N5, long N6)
{
	d=new MdaPrivate;
	d->q=this;
	d->do_construct();
    this->allocate(N1,N2,N3,N4,N5,N6);
}

Mda::Mda(const QString mda_filename)
{
    d=new MdaPrivate;
    d->q=this;
    d->do_construct();
    this->read(mda_filename);
}

Mda::Mda(const Mda &other)
{
	d=new MdaPrivate;
	d->q=this;
	d->do_construct();
	d->copy_from(other);
}

void Mda::operator=(const Mda &other)
{
	d->copy_from(other);
}

Mda::~Mda() {
	if (d->m_data) free(d->m_data);
	delete d;
}

bool Mda::allocate(long N1, long N2, long N3, long N4, long N5, long N6)
{
	d->m_dims[0]=N1;
	d->m_dims[1]=N2;
	d->m_dims[2]=N3;
	d->m_dims[3]=N4;
	d->m_dims[4]=N5;
	d->m_dims[5]=N6;
	d->m_total_size=N1*N2*N3*N4*N5*N6;

	if (d->m_data) free(d->m_data);
	d->m_data=0;
	if (d->m_total_size>0) {
		d->m_data=(double *)malloc(sizeof(double)*d->m_total_size);
		for (long i=0; i<d->m_total_size; i++) d->m_data[i]=0;
	}

	return true;

}

bool Mda::read(const QString &path)
{
	return read(path.toLatin1().data());
}

bool Mda::write32(const QString &path) const
{
	return write32(path.toLatin1().data());
}

bool Mda::write64(const QString &path) const
{
	return write64(path.toLatin1().data());
}

bool Mda::read(const char *path)
{
	FILE *input_file=fopen(path,"rb");
	if (!input_file) {
		printf("Warning: Unable to open mda file for reading: %s\n",path);
		return false;
	}
	MDAIO_HEADER H;
	mda_read_header(&H,input_file);
	this->allocate(H.dims[0],H.dims[1],H.dims[2],H.dims[3],H.dims[4],H.dims[5]);
	mda_read_float64(d->m_data,&H,d->m_total_size,input_file);
	fclose(input_file);
	return true;
}

bool Mda::write32(const char *path) const
{
	FILE *output_file=fopen(path,"wb");
	if (!output_file) {
		printf("Warning: Unable to open mda file for writing: %s\n",path);
		return false;
	}
	MDAIO_HEADER H;
	H.data_type=MDAIO_TYPE_FLOAT32;
	H.num_bytes_per_entry=4;
	for (int i=0; i<MDAIO_MAX_DIMS; i++) H.dims[i]=1;
	for (int i=0; i<MDA_MAX_DIMS; i++) H.dims[i]=d->m_dims[i];
	H.num_dims=d->determine_num_dims(N1(),N2(),N3(),N4(),N5(),N6());
	mda_write_header(&H,output_file);
	mda_write_float64(d->m_data,&H,d->m_total_size,output_file);
	fclose(output_file);
	return true;
}

bool Mda::write64(const char *path) const
{
	FILE *output_file=fopen(path,"wb");
	if (!output_file) {
		printf("Warning: Unable to open mda file for writing: %s\n",path);
		return false;
	}
	MDAIO_HEADER H;
	H.data_type=MDAIO_TYPE_FLOAT64;
	H.num_bytes_per_entry=4;
	for (int i=0; i<MDAIO_MAX_DIMS; i++) H.dims[i]=1;
	for (int i=0; i<MDA_MAX_DIMS; i++) H.dims[i]=d->m_dims[i];
	H.num_dims=d->determine_num_dims(N1(),N2(),N3(),N4(),N5(),N6());
	mda_write_header(&H,output_file);
	mda_write_float64(d->m_data,&H,d->m_total_size,output_file);
	fclose(output_file);
	return true;
}

int Mda::ndims() const
{
	return d->determine_num_dims(N1(),N2(),N3(),N4(),N5(),N6());
}

long Mda::N1() const
{
	return d->m_dims[0];
}

long Mda::N2() const
{
	return d->m_dims[1];
}

long Mda::N3() const
{
	return d->m_dims[2];
}

long Mda::N4() const
{
	return d->m_dims[3];
}

long Mda::N5() const
{
	return d->m_dims[4];
}

long Mda::N6() const
{
	return d->m_dims[5];
}

long Mda::totalSize() const
{
    return d->m_total_size;
}

long Mda::size(int dimension_index)
{
    if (dimension_index<0) return 0;
    if (dimension_index>=MDA_MAX_DIMS) return 1;
    return d->m_dims[dimension_index];
}

double Mda::get(long i) const
{
	return d->m_data[i];
}

double Mda::get(long i1, long i2) const
{
	return d->m_data[i1+d->m_dims[0]*i2];
}

double Mda::get(long i1, long i2, long i3) const
{
    return d->m_data[i1+d->m_dims[0]*i2+d->m_dims[0]*d->m_dims[1]*i3];
}

double Mda::get(long i1, long i2, long i3, long i4, long i5, long i6) const
{
	return d->m_data[
			i1+
			d->m_dims[0]*i2+
			d->m_dims[0]*d->m_dims[1]*i3+
			d->m_dims[0]*d->m_dims[1]*d->m_dims[2]*i4+
			d->m_dims[0]*d->m_dims[1]*d->m_dims[2]*d->m_dims[3]*i5+
			d->m_dims[0]*d->m_dims[1]*d->m_dims[2]*d->m_dims[3]*d->m_dims[4]*i6
			];
}

double Mda::value(long i) const
{
	if (!d->safe_index(i)) return 0;
	return get(i);
}

double Mda::value(long i1, long i2) const
{
	if (!d->safe_index(i1,i2)) return 0;
	return get(i1,i2);
}

double Mda::value(long i1, long i2, long i3) const
{
	if (!d->safe_index(i1,i2,i3)) return 0;
	return get(i1,i2,i3);
}

double Mda::value(long i1, long i2, long i3, long i4, long i5, long i6) const
{
	if (!d->safe_index(i1,i2,i3,i4,i5,i6)) return 0;
	return get(i1,i2,i3,i4,i5,i6);
}

void Mda::setValue(double val,long i)
{
	if (!d->safe_index(i)) return;
	set(val,i);
}

void Mda::setValue(double val,long i1, long i2)
{
	if (!d->safe_index(i1,i2)) return;
	set(val,i1,i2);
}

void Mda::setValue(double val,long i1, long i2, long i3)
{
	if (!d->safe_index(i1,i2,i3)) return;
	set(val,i1,i2,i3);
}

void Mda::setValue(double val,long i1, long i2, long i3, long i4, long i5, long i6)
{
	if (!d->safe_index(i1,i2,i3,i4,i5,i6)) return;
	set(val,i1,i2,i3,i4,i5,i6);
}

double *Mda::dataPtr()
{
	return d->m_data;
}

double *Mda::dataPtr(long i)
{
	return &d->m_data[i];
}

double *Mda::dataPtr(long i1, long i2)
{
	return &d->m_data[i1+N1()*i2];
}

double *Mda::dataPtr(long i1, long i2, long i3)
{
	return &d->m_data[i1+N1()*i2+N1()*N2()*i3];
}

double *Mda::dataPtr(long i1, long i2, long i3, long i4, long i5, long i6)
{
	return &d->m_data[
			i1+
			N1()*i2+
			N1()*N2()*i3+
			N1()*N2()*N3()*i4+
			N1()*N2()*N3()*N4()*i5+
			N1()*N2()*N3()*N4()*N5()*i6
			];
}

void Mda::getChunk(Mda &ret, long i, long size)
{
	long a_begin=i; long x_begin=0;
	long a_end=i+size-1; long x_end=size-1;

	if (a_begin<0) {a_begin+=0-a_begin; x_begin+=0-a_begin;}
	if (a_end>=d->m_total_size) {a_end+=d->m_total_size-1-a_end; x_end+=d->m_total_size-1-a_end;}

	ret.allocate(1,size);

	double *ptr1=this->dataPtr();
	double *ptr2=ret.dataPtr();

	long ii=0;
	for (long a=a_begin; a<=a_end; a++) {
		ptr2[x_begin+ii]=ptr1[a_begin+ii];
	}
}

void Mda::getChunk(Mda &ret, long i1, long i2, long size1, long size2)
{
	long a1_begin=i1; long x1_begin=0;
	long a1_end=i1+size1-1; long x1_end=size1-1;
	if (a1_begin<0) {a1_begin+=0-a1_begin; x1_begin+=0-a1_begin;}
	if (a1_end>=N1()) {a1_end+=N1()-1-a1_end; x1_end+=N1()-1-a1_end;}

	long a2_begin=i2; long x2_begin=0;
	long a2_end=i2+size2-1; long x2_end=size2-1;
	if (a2_begin<0) {a2_begin+=0-a2_begin; x2_begin+=0-a2_begin;}
	if (a2_end>=N2()) {a2_end+=N2()-1-a2_end; x2_end+=N2()-1-a2_end;}

	ret.allocate(size1,size2);

	double *ptr1=this->dataPtr();
	double *ptr2=ret.dataPtr();

	for (long ind2=0; ind2<=a2_end-a2_begin; ind2++) {
		long ii_out=(ind2+x2_begin)*size1;
		long ii_in=(ind2+a2_begin)*N1();
		for (long ind1=0; ind1<=a1_end-a1_begin; ind1++) {
			ptr2[ii_out]=ptr1[ii_in];
			ii_in++;
			ii_out++;
		}
	}
}

void Mda::getChunk(Mda &ret, long i1, long i2, long i3, long size1, long size2, long size3)
{
	long a1_begin=i1; long x1_begin=0;
	long a1_end=i1+size1-1; long x1_end=size1-1;
	if (a1_begin<0) {a1_begin+=0-a1_begin; x1_begin+=0-a1_begin;}
	if (a1_end>=N1()) {a1_end+=N1()-1-a1_end; x1_end+=N1()-1-a1_end;}

	long a2_begin=i2; long x2_begin=0;
	long a2_end=i2+size2-1; long x2_end=size2-1;
	if (a2_begin<0) {a2_begin+=0-a2_begin; x2_begin+=0-a2_begin;}
	if (a2_end>=N2()) {a2_end+=N2()-1-a2_end; x2_end+=N2()-1-a2_end;}

	long a3_begin=i3; long x3_begin=0;
	long a3_end=i3+size3-1; long x3_end=size3-1;
	if (a3_begin<0) {a2_begin+=0-a3_begin; x3_begin+=0-a3_begin;}
	if (a3_end>=N3()) {a3_end+=N3()-1-a3_end; x3_end+=N3()-1-a3_end;}

	ret.allocate(size1,size2,size3);

	double *ptr1=this->dataPtr();
	double *ptr2=ret.dataPtr();

	for (long ind3=0; ind3<=a3_end-a3_begin; ind3++) {
		for (long ind2=0; ind2<=a2_end-a2_begin; ind2++) {
			long ii_out=(ind2+x2_begin)*size1+(ind3+x3_begin)*size1*size2;
			long ii_in=(ind2+a2_begin)*N1()+(ind3+a3_begin)*N1()*N2();
			for (long ind1=0; ind1<=a1_end-a1_begin; ind1++) {
				ptr2[ii_out]=ptr1[ii_in];
				ii_in++;
				ii_out++;
			}
		}
    }
}

void Mda::setChunk(Mda &X, long i)
{
    long size=X.totalSize();

    long a_begin=i; long x_begin=0;
    long a_end=i+size-1; long x_end=size-1;

    if (a_begin<0) {a_begin+=0-a_begin; x_begin+=0-a_begin;}
    if (a_end>=d->m_total_size) {a_end+=d->m_total_size-1-a_end; x_end+=d->m_total_size-1-a_end;}

    double *ptr1=this->dataPtr();
    double *ptr2=X.dataPtr();

    long ii=0;
    for (long a=a_begin; a<=a_end; a++) {
        ptr1[a_begin+ii]=ptr2[x_begin+ii];
    }
}

void Mda::setChunk(Mda &X, long i1, long i2)
{
    long size1=X.N1();
    long size2=X.N2();

    long a1_begin=i1; long x1_begin=0;
    long a1_end=i1+size1-1; long x1_end=size1-1;
    if (a1_begin<0) {a1_begin+=0-a1_begin; x1_begin+=0-a1_begin;}
    if (a1_end>=N1()) {a1_end+=N1()-1-a1_end; x1_end+=N1()-1-a1_end;}

    long a2_begin=i2; long x2_begin=0;
    long a2_end=i2+size2-1; long x2_end=size2-1;
    if (a2_begin<0) {a2_begin+=0-a2_begin; x2_begin+=0-a2_begin;}
    if (a2_end>=N2()) {a2_end+=N2()-1-a2_end; x2_end+=N2()-1-a2_end;}

    double *ptr1=this->dataPtr();
    double *ptr2=X.dataPtr();

    for (long ind2=0; ind2<=a2_end-a2_begin; ind2++) {
        long ii_out=(ind2+x2_begin)*size1;
        long ii_in=(ind2+a2_begin)*N1();
        for (long ind1=0; ind1<=a1_end-a1_begin; ind1++) {
            ptr1[ii_in]=ptr2[ii_out];
            ii_in++;
            ii_out++;
        }
    }
}

void Mda::setChunk(Mda &X, long i1, long i2, long i3)
{
    long size1=X.N1();
    long size2=X.N2();
    long size3=X.N3();

    long a1_begin=i1; long x1_begin=0;
    long a1_end=i1+size1-1; long x1_end=size1-1;
    if (a1_begin<0) {a1_begin+=0-a1_begin; x1_begin+=0-a1_begin;}
    if (a1_end>=N1()) {a1_end+=N1()-1-a1_end; x1_end+=N1()-1-a1_end;}

    long a2_begin=i2; long x2_begin=0;
    long a2_end=i2+size2-1; long x2_end=size2-1;
    if (a2_begin<0) {a2_begin+=0-a2_begin; x2_begin+=0-a2_begin;}
    if (a2_end>=N2()) {a2_end+=N2()-1-a2_end; x2_end+=N2()-1-a2_end;}

    long a3_begin=i3; long x3_begin=0;
    long a3_end=i3+size3-1; long x3_end=size3-1;
    if (a3_begin<0) {a2_begin+=0-a3_begin; x3_begin+=0-a3_begin;}
    if (a3_end>=N3()) {a3_end+=N3()-1-a3_end; x3_end+=N3()-1-a3_end;}

    double *ptr1=this->dataPtr();
    double *ptr2=X.dataPtr();

    for (long ind3=0; ind3<=a3_end-a3_begin; ind3++) {
        for (long ind2=0; ind2<=a2_end-a2_begin; ind2++) {
            long ii_out=(ind2+x2_begin)*size1+(ind3+x3_begin)*size1*size2;
            long ii_in=(ind2+a2_begin)*N1()+(ind3+a3_begin)*N1()*N2();
            for (long ind1=0; ind1<=a1_end-a1_begin; ind1++) {
                ptr1[ii_in]=ptr2[ii_out];
                ii_in++;
                ii_out++;
            }
        }
    }
}

void Mda::reshape(int N1b, int N2b, int N3b, int N4b, int N5b, int N6b)
{
    if (N1b*N2b*N3b*N4b*N5b*N6b!=this->totalSize()) {
        qWarning() << "Unable to reshape, wrong total size";
        qWarning() << N1b << N2b << N3b << N4b << N5b << N6b;
        qWarning() << N1() << N2() << N3() << N4() << N5() << N6();
        return;
    }
    d->m_dims[0]=N1b;
    d->m_dims[1]=N2b;
    d->m_dims[2]=N3b;
    d->m_dims[3]=N4b;
    d->m_dims[4]=N5b;
    d->m_dims[5]=N6b;
}

void Mda::set(double val, long i)
{
	d->m_data[i]=val;
}

void Mda::set(double val, long i1, long i2)
{
	d->m_data[i1+d->m_dims[0]*i2]=val;
}

void Mda::set(double val, long i1, long i2, long i3)
{
	d->m_data[i1+d->m_dims[0]*i2+d->m_dims[0]*d->m_dims[1]*i3]=val;
}

void Mda::set(double val, long i1, long i2, long i3, long i4, long i5, long i6)
{
	d->m_data[
			i1+
			d->m_dims[0]*i2+
			d->m_dims[0]*d->m_dims[1]*i3+
			d->m_dims[0]*d->m_dims[1]*d->m_dims[2]*i4+
			d->m_dims[0]*d->m_dims[1]*d->m_dims[2]*d->m_dims[3]*i5+
			d->m_dims[0]*d->m_dims[1]*d->m_dims[2]*d->m_dims[3]*d->m_dims[4]*i6
			]
	=val;
}

void MdaPrivate::do_construct() {
	m_data=(double *)malloc(sizeof(double)*1);
	for (int i=0; i<MDA_MAX_DIMS; i++) {
		m_dims[i]=1;
	}
	m_total_size=1;
}

void MdaPrivate::copy_from(const Mda &other)
{
	if (m_data) free(m_data);
	m_data=0;
	m_total_size=other.d->m_total_size;
	for (int i=0; i<MDA_MAX_DIMS; i++) m_dims[i]=other.d->m_dims[i];
	if (m_total_size>0) {
		m_data=(double *)malloc(sizeof(double)*m_total_size);
		memcpy(m_data,other.d->m_data,sizeof(double)*m_total_size);
	}
}

int MdaPrivate::determine_num_dims(long N1, long N2, long N3, long N4, long N5, long N6)
{
	#ifdef QT_CORE_LIB
	Q_UNUSED(N1)
	Q_UNUSED(N2)
	#endif
	if (N6>1) return 6;
	if (N5>1) return 5;
	if (N4>1) return 4;
	if (N3>1) return 3;
	return 2;
}

bool MdaPrivate::safe_index(long i)
{
	return ((0<=i)&&(i<m_total_size));
}

bool MdaPrivate::safe_index(long i1, long i2)
{
	return ((0<=i1)&&(i1<m_dims[0])&&(0<=i2)&&(i2<m_dims[1]));
}

bool MdaPrivate::safe_index(long i1, long i2, long i3)
{
	return ((0<=i1)&&(i1<m_dims[0])&&(0<=i2)&&(i2<m_dims[1])&&(0<=i3)&&(i3<m_dims[2]));
}

bool MdaPrivate::safe_index(long i1, long i2, long i3, long i4, long i5, long i6)
{
	return (
		(0<=i1)&&(i1<m_dims[0])&&
		(0<=i2)&&(i2<m_dims[1])&&
		(0<=i3)&&(i3<m_dims[2])&&
		(0<=i4)&&(i4<m_dims[3])&&
		(0<=i5)&&(i5<m_dims[4])&&
		(0<=i6)&&(i6<m_dims[5])
	);
}
