/* ----------------------------- MNI Header -----------------------------------
@NAME       :  itk_resample
@DESCRIPTION:  an example of using spline itnerpolation with MINC xfm transform
@COPYRIGHT  :
              Copyright 2011 Vladimir Fonov, McConnell Brain Imaging Centre, 
              Montreal Neurological Institute, McGill University.
              Permission to use, copy, modify, and distribute this
              software and its documentation for any purpose and without
              fee is hereby granted, provided that the above copyright
              notice appear in all copies.  The author and McGill University
              make no representations about the suitability of this
              software for any purpose.  It is provided "as is" without
              express or implied warranty.
---------------------------------------------------------------------------- */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif 

#include <iostream>
#include <fstream>
#include <getopt.h>
#include <vector>
#include <set>
#include <valarray>
#include <math.h>
#include <limits>
#include <unistd.h>
#include <algorithm>

#include <itkVector.h>
#include <itkResampleImageFilter.h>
#include <itkAffineTransform.h>
#include <itkIdentityTransform.h>
#include <itkNearestNeighborInterpolateImageFunction.h>
#include <itkBSplineInterpolateImageFunction.h>
#include <itkVectorImage.h>
#include <itkBinaryThresholdImageFilter.h>
#include <itkVectorResampleImageFilter.h>
// #include "mincVectorBSplineInterpolate.h"
#include <itkImageConstIterator.h>
#include <itkMetaDataDictionary.h>
#include <itkMetaDataObject.h>

#include <unistd.h>
#include <getopt.h>

#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkImageIOFactory.h>
#include <itkImageIOBase.h>

#include <vnl/vnl_cross.h>
#include <itkCompositeTransform.h>

typedef itk::ImageBase<3>   Image3DBase;
typedef itk::Image<float,3> Float3DImage;
typedef itk::Image<int,3>   Int3DImage;
typedef itk::Image<short,3> Short3DImage;
typedef itk::Image<unsigned char,3> Byte3DImage;
typedef itk::Image<itk::Vector<float,3>, 3>  Vector3DImage;
//typedef itk::VectorImage<float, 3>  Vector3DImage;


typedef itk::ImageIOBase          IOBase;
typedef itk::SmartPointer<IOBase> IOBasePointer;

typedef itk::BSplineInterpolateImageFunction< Float3DImage, double, double >  InterpolatorType;
typedef itk::NearestNeighborInterpolateImageFunction< Int3DImage, double >    NNInterpolatorType;
// typedef minc::mincVectorBSplineInterpolate<Vector3DImage,double>      VectorInterpolatorType;
typedef itk::ResampleImageFilter<Float3DImage, Float3DImage> FloatFilterType;
typedef itk::ResampleImageFilter<Int3DImage  , Int3DImage>   IntFilterType;
typedef itk::VectorResampleImageFilter<Vector3DImage , Vector3DImage>  VectorFilterType;

typedef itk::IdentityTransform<double,3> IdentityTransformType;
  
typedef itk::CompositeTransform< double, 3 > TransformType;

using namespace  std;

std::string minc_timestamp(int argc,char **argv)
{
  std::string timestamp;
  
  char cur_time[200];
  time_t t;
  struct tm *tmp;

  t = time(NULL);
  tmp = localtime(&t);
  
  strftime(cur_time, sizeof(cur_time), "%a %b %d %T %Y>>>", tmp);
  /* Get the time, overwriting newline */
  timestamp=cur_time;
  
  /* Copy the program name and arguments */
  for (int i=0; i<argc; i++) {
    timestamp+=argv[i];
    timestamp+=" ";
  }
  timestamp+="\n";
  
  return timestamp;
}


void show_usage (const char * prog)
{
  std::cerr 
    << "Usage: "<<prog<<" <input> <output.mnc> " << std::endl
    << "--clobber overwrite files"    << std::endl
    << "--like <example> (default behaviour analogous to use_input_sampling)"<<std::endl
    << "--transform <xfm_transform> "<<std::endl
    << "--order <n> spline order, default 2 "<<std::endl
    << "--uniformize <step> - will make a volume with uniform step size and no direction cosines" << std::endl
    << "--normalize - outputs volume with right-hand direction cosines and positive step-sizes, uses NN interpolation" << std::endl
    << "--invert_transform  - apply inverted transform"<<std::endl
    << "--labels - assume that input data is discrete labels, will use Nearest-Neighbour interpolator"<<std::endl
    << "--byte  - store image in byte  voxels minc file"<<std::endl
    << "--short - store image in short voxels minc file"<<std::endl
    << "--float - store image in float voxels minc file"<<std::endl;
}

template<class T,class I> void generate_uniform_sampling(T* flt, const I* img,double step)
{
  //obtain physical coordinats of all image corners
  typename I::RegionType r=img->GetLargestPossibleRegion();
  std::vector<double> corner[3];
  for(int i=0;i<8;i++)
  {
    typename I::IndexType idx;
    typename I::PointType c;
    idx[0]=r.GetIndex()[0]+r.GetSize()[0]*(i%2);
    idx[1]=r.GetIndex()[1]+r.GetSize()[1]*((i/2)%2);
    idx[2]=r.GetIndex()[2]+r.GetSize()[2]*((i/4)%2);
    img->TransformIndexToPhysicalPoint(idx,c);
    for(int j=0;j<3;j++)
      corner[j].push_back(c[j]);
  }
  typename I::IndexType start;
  typename T::SizeType size;
  typename T::OriginPointType org;
  typename I::SpacingType spc;
  spc.Fill(step);
  for(int j=0;j<3;j++)
  {
    std::sort(corner[j].begin(),corner[j].end());
    size[j]=ceil((corner[j][7]-corner[j][0])/step);
    org[j]=corner[j][0];
  }
  Float3DImage::DirectionType identity;
  identity.SetIdentity();
  flt->SetOutputDirection(identity);
  start.Fill(0);
  flt->SetOutputStartIndex(start);
  flt->SetSize(size);
  flt->SetOutputOrigin(org);
  flt->SetOutputSpacing(spc);
}

template<class D> bool abs_compare(const D& a,const D&b) { return fabs(a)<fabs(b);}

template<class T,class I> void generate_normalized_sampling(T* flt, const I* img)
{
  //obtain physical coordinats of all image corners
  typedef typename I::PointType  PointType;
  typedef typename I::IndexType  IndexType;
  typedef typename I::PointType::VectorType VectorType;
  typename I::RegionType r=img->GetLargestPossibleRegion();
  
  int i[3];
  //check all combinations of corners
  
  for(i[0]=0;i[0]<2;i[0]++)
    for(i[1]=0;i[1]<2;i[1]++)
      for(i[2]=0;i[2]<2;i[2]++)
  {
    IndexType idx,idx2[3];
    PointType corner,c2[3];
    VectorType v[3];
    double   dist[3];
    int      remap[3],rrmap[3];
    double   proj[3];
    
    for(int j=0;j<3;j++)
    {
      idx[j]=r.GetIndex()[j]+r.GetSize()[j]*i[j];
      remap[j]=-1;
      rrmap[j]=-1;
      proj[j]=0.0;
    }
    img->TransformIndexToPhysicalPoint(idx,corner);
    
    for(int j=0;j<3;j++)
    {
      idx2[j]=idx;
      idx2[j][j]=r.GetIndex()[j]+r.GetSize()[j]*(i[j]^1);
      
      img->TransformIndexToPhysicalPoint(idx2[j],c2[j]);
      
      dist[j]=corner.EuclideanDistanceTo(c2[j]);
      
      v[j]=(c2[j]-corner)/dist[j];
      //sort according to orientation
      size_t mp=std::max_element(v[j].GetDataPointer(),v[j].GetDataPointer()+3,abs_compare<typename VectorType::RealValueType>)-(v[j].GetDataPointer());
      
      if(rrmap[mp]==-1) //we have already found this axis?
      {
        remap[j]=mp;
        rrmap[mp]=j;
        proj[j]=v[j][mp];
      } else {
        std::cout<<"Failed to find max component!"<<std::endl;
        std::cout<<"mp="<<mp<<std::endl;
      }
    }
    //check if all axis are mapped
    for(int j=0;j<3;j++)
    {
      if(remap[j]==-1)//assign next available axis
      {
        int k;
        for(k=0;k<3&&rrmap[k]!=-1;k++);
        if(k==3) //abort?
          throw itk::ExceptionObject("Wierd orientation, can't figure out how to normalize it");
        remap[j]=k;
        rrmap[k]=j;
      }
    }
    
    vnl_vector_fixed<typename VectorType::RealValueType,3> xvec=v[rrmap[0]].GetVnlVector();
    vnl_vector_fixed<typename VectorType::RealValueType,3> yvec=v[rrmap[1]].GetVnlVector();
    vnl_vector_fixed<typename VectorType::RealValueType,3> zvec=v[rrmap[2]].GetVnlVector();
    
    //check if coordinate system is positive (right handed) i.e determinant is positive
    //and that projections on axes are posive
    if(dot_product(vnl_cross_3d(xvec,yvec),zvec)>0 &&
       proj[0]>0 &&
       proj[1]>0 &&
       proj[2]>0 )
    {
      //found best candidate
      //now calculate the bounds
      
      typename I::IndexType start;
      typename T::SizeType size;
      typename I::SpacingType spc;
      
      Float3DImage::DirectionType dir;
      for(int j=0;j<3;j++)
      {
        dir[j][0]=xvec[j];
        dir[j][1]=yvec[j];
        dir[j][2]=zvec[j];
        
        size[j]=r.GetSize()[rrmap[j]];
        
        //spc[j]=dist[rrmap[j]]/size[j];//img->GetSpacing()[rrmap[j]];
        spc[j]=img->GetSpacing()[rrmap[j]];
      }
      flt->SetOutputDirection(dir);
      
      start.Fill(0);
      flt->SetOutputStartIndex(start);
      
      flt->SetSize(size);
      flt->SetOutputOrigin(corner);
      flt->SetOutputSpacing(spc);
      
      return;
    }
  }
}



template<class Image,class ImageOut,class Interpolator> 
void resample_image(
   IOBase* base,  
   const std::string& output_f,
   const std::string& xfm_f,
   const std::string& like_f,
   bool invert,
   double uniformize,
   bool normalize,
   const char* history,
   bool store_float,
   bool store_short,
   bool store_byte,
   Interpolator* interpolator)
{
  typedef typename itk::ResampleImageFilter<Image, ImageOut> ResampleFilterType;
  typedef typename itk::ImageFileReader<Image >   ImageReaderType;
  typedef typename itk::ImageFileWriter<ImageOut >   ImageWriterType;
  
  typename ImageReaderType::Pointer reader = ImageReaderType::New();
  
  //initializing the reader
  reader->SetImageIO(base);
  reader->SetFileName(base->GetFileName());
  reader->Update();
  
  typename Image::Pointer in=reader->GetOutput();

  typename ResampleFilterType::Pointer filter  = ResampleFilterType::New();
  
  //creating coordinate transformation objects
  TransformType::Pointer transform = TransformType::New();  
  IdentityTransformType::Pointer identity_transform = IdentityTransformType::New();
  
  if(!xfm_f.empty())
  {
    //TODO: implement this
    std::cerr<<"MINC XFM IO not implemented yet"<<std::endl;
  } else {
    filter->SetTransform( identity_transform );
  }

  //creating the interpolator
  filter->SetInterpolator( interpolator );
  filter->SetDefaultPixelValue( 0 );
  //this is for processing using batch system
  filter->SetNumberOfThreads(1);

  typename Image::Pointer like=0;
  if(!like_f.empty())
  {
    typename ImageReaderType::Pointer reader = ImageReaderType::New();
    reader->SetFileName(like_f.c_str());
    reader->Update();
    if(uniformize!=0.0)
    {
      generate_uniform_sampling<ResampleFilterType,Image>(filter,reader->GetOutput(),uniformize);
    } else if(normalize) {
      generate_normalized_sampling<ResampleFilterType,Image>(filter,reader->GetOutput());
    } else {
      filter->SetOutputParametersFromImage(reader->GetOutput());
      filter->SetOutputDirection(reader->GetOutput()->GetDirection());
    }
    like=reader->GetOutput();
    like->DisconnectPipeline();
  } else {
    if(uniformize!=0.0)
    {
      generate_uniform_sampling<ResampleFilterType,Image>(filter,in,uniformize);
    } else if(normalize) {
      generate_normalized_sampling<ResampleFilterType,Image>(filter,in);
    } else {
      //we are using original sampling
      filter->SetOutputParametersFromImage(in);
      filter->SetOutputDirection(in->GetDirection());
    }
  }
  
  filter->SetInput(in);

  filter->Update();
  typename ImageOut::Pointer out=filter->GetOutput();

  //MINC specific stuff
  out->SetMetaDataDictionary(in->GetMetaDataDictionary());
  itk::EncapsulateMetaData<std::string>(out->GetMetaDataDictionary(),"storage_data_type",store_byte?typeid(unsigned char).name():(store_short?typeid(unsigned short).name():typeid(float).name()));
  
  std::string old_history;
  itk::ExposeMetaData<std::string >(out->GetMetaDataDictionary(),"history",old_history);
  old_history+="\n";
  old_history+=history;
  itk::EncapsulateMetaData<std::string>(out->GetMetaDataDictionary(),"history",old_history);
  
  
  //generic file writer
  typename ImageWriterType::Pointer writer = ImageWriterType::New();
  writer->SetFileName(output_f.c_str());
  
  writer->SetInput( out );
  writer->Update();
}

template<class Image,class TmpImage,class ImageOut,class Interpolator> 
void resample_label_image (
   IOBase* base,
   const std::string& output_f,
   const std::string& xfm_f,
   const std::string& like_f,
   bool invert,
   double uniformize,
   bool normalize,
   const char* history,
   bool store_float,
   bool store_short,
   bool store_byte,
   Interpolator* interpolator)
{
  typedef typename itk::ResampleImageFilter<TmpImage, TmpImage> ResampleFilterType;
  typedef typename itk::ImageFileReader<Image >      ImageReaderType;
  typedef typename itk::ImageFileWriter<ImageOut >   ImageWriterType;
  typedef itk::ImageRegionConstIterator<Image>    ConstInputImageIteratorType;
  typedef itk::ImageRegionConstIterator<TmpImage> ConstTmpImageIteratorType;
  typedef itk::ImageRegionIterator<TmpImage>       TmpImageIteratorType;
  
  typedef itk::ImageRegionIterator<ImageOut>              ImageOutIteratorType;
  typedef itk::BinaryThresholdImageFilter<Image,TmpImage> ThresholdFilterType;

  typedef typename Image::PixelType InputPixelType;
  typename ImageReaderType::Pointer reader = ImageReaderType::New();

  //initializing the reader
  reader->SetImageIO(base);
  reader->SetFileName(base->GetFileName());
  reader->Update();

  typename Image::Pointer in=reader->GetOutput();

  typename ResampleFilterType::Pointer  filter           = ResampleFilterType::New();
  typename ThresholdFilterType::Pointer threshold_filter = ThresholdFilterType::New();

  //creating coordinate transformation objects
  TransformType::Pointer transform = TransformType::New();
  if(!xfm_f.empty())
  {
    //TODO: implement this
    std::cerr<<"MINC XFM IO not implemented yet"<<std::endl;
  }

  //creating the interpolator
  filter->SetInterpolator( interpolator );
  filter->SetDefaultPixelValue( 0 );
  
  //this is for processing using batch system
  filter->SetNumberOfThreads(1);

  typename Image::Pointer like=0;
  
  
  if(!like_f.empty())
  {
    typename ImageReaderType::Pointer reader = ImageReaderType::New();
    reader->SetFileName(like_f.c_str());
    reader->Update();
    if(uniformize!=0.0)
    {
      generate_uniform_sampling<ResampleFilterType,Image>(filter,reader->GetOutput(),uniformize);
    } else if(normalize) {
      generate_normalized_sampling<ResampleFilterType,Image>(filter,reader->GetOutput());
    } else {
      filter->SetOutputParametersFromImage(reader->GetOutput());
      filter->SetOutputDirection(reader->GetOutput()->GetDirection());
    }
    like=reader->GetOutput();
    like->DisconnectPipeline();
  }
  else
  {
    if(uniformize!=0.0)
    {
      generate_uniform_sampling<ResampleFilterType,Image>(filter,in,uniformize);
    } else if(normalize) {
      generate_normalized_sampling<ResampleFilterType,Image>(filter,in);
    } else {
      //we are using original sampling
      filter->SetOutputParametersFromImage(in);
      filter->SetOutputDirection(in->GetDirection());
    }
  }
  typename TmpImage::RegionType region;
  region.SetSize (filter->GetSize());
  region.SetIndex(filter->GetOutputStartIndex());
  
  
  typename TmpImage::Pointer MaxImage= TmpImage::New();
  MaxImage->SetOrigin(filter->GetOutputOrigin());
  MaxImage->SetSpacing(filter->GetOutputSpacing());
  MaxImage->SetDirection(filter->GetOutputDirection());
  
  MaxImage->SetLargestPossibleRegion(region);
  MaxImage->SetBufferedRegion(region);
  MaxImage->SetRequestedRegion(region);
  MaxImage->Allocate();
  MaxImage->FillBuffer(0.0);
  
  typename ImageOut::Pointer LabelImage= ImageOut::New();
  LabelImage->SetOrigin(filter->GetOutputOrigin());
  LabelImage->SetSpacing(filter->GetOutputSpacing());
  LabelImage->SetDirection(filter->GetOutputDirection());
  
  LabelImage->SetLargestPossibleRegion(region);
  LabelImage->SetBufferedRegion(region);
  LabelImage->SetRequestedRegion(region);
  LabelImage->Allocate();
  LabelImage->FillBuffer(0);
  
  //split the input image
  std::set<InputPixelType> sval;
  for(ConstInputImageIteratorType it(in, in->GetBufferedRegion()); !it.IsAtEnd(); ++it)
  {
    InputPixelType val = it.Get();

    if(vnl_math_isfinite(val))
      sval.insert(val);
  }
  
  //std::vector<typename TmpImage::Pointer> list_of_labels;
  // Generate a bunch of binary copies (unfortunately, we store them as double)
  for(typename set<InputPixelType>::iterator label_it = sval.begin(); label_it != sval.end(); ++label_it)
  {
    threshold_filter->SetLowerThreshold(*label_it);
    threshold_filter->SetUpperThreshold(*label_it);
    threshold_filter->SetInsideValue(1.0);
    threshold_filter->SetOutsideValue(0.0);
    threshold_filter->SetInput(in);
    
    filter->SetInput(threshold_filter->GetOutput());
    filter->Update();
    
    ConstTmpImageIteratorType it_filter(filter->GetOutput(), filter->GetOutput()->GetBufferedRegion());
    TmpImageIteratorType      it_max(MaxImage, MaxImage->GetBufferedRegion());
    ImageOutIteratorType      it_out(LabelImage, LabelImage->GetBufferedRegion());
    for(; !it_filter.IsAtEnd(); ++it_filter,++it_max,++it_out)
    {
      typename TmpImage::PixelType val = it_filter.Get();
      typename TmpImage::PixelType val_max = it_max.Get();
      if(label_it == sval.begin() || vnl_math_isfinite(val) && val>val_max)
      {
        it_max.Set(val);
        it_out.Set(*label_it);
      }
    }
  }

  //MINC specific stuff
  LabelImage->SetMetaDataDictionary(in->GetMetaDataDictionary());
  itk::EncapsulateMetaData<std::string>(LabelImage->GetMetaDataDictionary(),"storage_data_type",store_byte?typeid(unsigned char).name():(store_short?typeid(unsigned short).name():typeid(unsigned int).name()));
  
  std::string old_history;
  itk::ExposeMetaData<std::string >(LabelImage->GetMetaDataDictionary(),"history",old_history);
  old_history+="\n";
  old_history+=history;
  itk::EncapsulateMetaData<std::string>(LabelImage->GetMetaDataDictionary(),"history",old_history);
  

  //generic file writer
  typename ImageWriterType::Pointer writer = ImageWriterType::New();
  writer->SetFileName(output_f.c_str());
  

  writer->SetInput( LabelImage );
  writer->Update();
}



// template<class Image,class ImageOut,class Interpolator> 
// void resample_vector_image(
//    IOBase* base,  
//    const std::string& output_f,
//    const std::string& xfm_f,
//    const std::string& like_f,
//    bool invert,
//    double uniformize,
//    bool normalize,
//    const char* history,
//    bool store_float,
//    bool store_short,
//    bool store_byte,
//    Interpolator* interpolator)
// {
//   //typedef typename itk::VariableVectorResampleImageFilter<Image, ImageOut> ResampleFilterType;
//   typedef typename itk::VectorResampleImageFilter<Image, ImageOut> ResampleFilterType;
//   typedef typename itk::ImageFileReader<Image >   ImageReaderType;
//   typedef typename itk::ImageFileWriter<ImageOut >   ImageWriterType;
//   
//   typename ImageReaderType::Pointer reader = ImageReaderType::New();
//   
//   //initializing the reader
//   reader->SetImageIO(base);
//   reader->SetFileName(base->GetFileName());
//   reader->Update();
//   
//   typename Image::Pointer in=reader->GetOutput();
// 
//   typename ResampleFilterType::Pointer filter  = ResampleFilterType::New();
//   
//   //creating coordinate transformation objects
//   TransformType::Pointer transform = TransformType::New();
//   if(!xfm_f.empty())
//   {
//     //reading a minc style xfm file
//     std::cerr<<"MINC XFM IO not implemented yet!"<<std::endl;
//   }
// 
//   //creating the interpolator
// 
//   filter->SetInterpolator( interpolator );
//   //filter->SetDefaultPixelValue( 0 );
//   
//   //this is for processing using batch system
//   filter->SetNumberOfThreads(1);
//   
//   typename Image::Pointer like=0;
//   if(!like_f.empty())
//   {
//     typename ImageReaderType::Pointer reader = ImageReaderType::New();
//     reader->SetFileName(like_f.c_str());
//     reader->Update();
//     if(uniformize!=0.0)
//     {
//       generate_uniform_sampling<ResampleFilterType,Image>(filter,reader->GetOutput(),uniformize);
//     } else if(normalize) {
//       generate_normalized_sampling<ResampleFilterType,Image>(filter,reader->GetOutput());
//     } else {
//       filter->SetOutputSpacing(reader->GetOutput()->GetSpacing());
//       filter->SetOutputOrigin(reader->GetOutput()->GetOrigin());
//       filter->SetOutputDirection(reader->GetOutput()->GetDirection());
//     }
//     like=reader->GetOutput();
//     like->DisconnectPipeline();
//   }
//   else
//   {
//     if(uniformize!=0.0)
//     {
//       generate_uniform_sampling<ResampleFilterType,Image>(filter,in,uniformize);
//     } else if(normalize) {
//       generate_normalized_sampling<ResampleFilterType,Image>(filter,in);
//     } else {
//       //we are using original sampling
//       filter->SetOutputSpacing(in->GetSpacing());
//       filter->SetOutputOrigin(in->GetOrigin());
//       filter->SetOutputDirection(in->GetDirection());
//     }
//   }
//   filter->SetInput(in);
//   filter->Update();
//   typename ImageOut::Pointer out=filter->GetOutput();
//   typename ImageWriterType::Pointer writer = ImageWriterType::New();
//   writer->SetFileName(output_f.c_str());
//   
//   writer->SetInput( out );
//   writer->Update();
// }


int main (int argc, char **argv)
{
  int store_float=0;
  int store_short=0;
  int store_byte=0;
  
  int verbose=0, clobber=0,skip_grid=0;
  int order=2;
  std::string like_f,xfm_f,output_f,input_f;
  double uniformize=0.0;
  int normalize=0;
  int invert=0;
  int labels=0;
  std::string history=minc_timestamp(argc,argv);
  
  bool order_was_set=false;
  
  static struct option long_options[] = {
		{"verbose", no_argument,       &verbose, 1},
		{"quiet",   no_argument,       &verbose, 0},
		{"clobber", no_argument,       &clobber, 1},
		{"like",    required_argument, 0, 'l'},
		{"transform",    required_argument, 0, 't'},
    {"order",    required_argument, 0, 'o'},
    {"uniformize",    required_argument, 0, 'u'},
    {"invert_transform", no_argument, &invert, 1},
    {"normalize", no_argument, &normalize, 1},
    {"labels",no_argument, &labels, 1},
    {"float",   no_argument, &store_float, 1},
    {"short",   no_argument, &store_short, 1},
    {"byte",    no_argument, &store_byte,  1},
    
    {0, 0, 0, 0}
    };
  
  for (;;) {
      /* getopt_long stores the option index here. */
      int option_index = 0;

      int c = getopt_long (argc, argv, "vqcl:t:o:u:", long_options, &option_index);

      /* Detect the end of the options. */
      if (c == -1) break;

      switch (c)
			{
			case 0:
				break;
			case 'v':
				cout << "Version: 0.8" << endl;
				return 0;
      case 'l':
        like_f=optarg; break;
      case 't':
        xfm_f=optarg; break;
      case 'o':
        order=atoi(optarg);
        order_was_set=true;
        break;
      case 'u':
        uniformize=atof(optarg);break;
			case '?':
				/* getopt_long already printed an error message. */
			default:
				show_usage (argv[0]);
				return 1;
			}
    }

  if(normalize && ! order_was_set) //perform nearest neighbour interpolation in this case
    order=0;
  
	if ((argc - optind) < 2) {
		show_usage(argv[0]);
		return 1;
	}
  input_f=argv[optind];
  output_f=argv[optind+1];
  
  if (!clobber && !access (output_f.c_str (), F_OK))
  {
    std::cerr << output_f.c_str () << " Exists!" << std::endl;
    return 1;
  }
  
	try
  {
    //try to figure out what we have got
    IOBasePointer io = itk::ImageIOFactory::CreateImageIO(input_f.c_str(), itk::ImageIOFactory::ReadMode );
    
    if(!io)
      throw itk::ExceptionObject("Unsupported image file type");
    
    io->SetFileName(input_f.c_str());
    io->ReadImageInformation();

    size_t nd = io->GetNumberOfDimensions();
    size_t nc = io->GetNumberOfComponents();
    itk::ImageIOBase::IOComponentType  ct = io->GetComponentType();
    itk::ImageIOBase::IOComponentType  oct = ct;
    
    if( nc==1 && nd==3 ) //3D image, simple case
    {
      if(labels)
      {
        if(order==0 || !order_was_set)
        {
          //creating the interpolator
          NNInterpolatorType::Pointer interpolator = NNInterpolatorType::New();
          if(store_byte)
            resample_image<Int3DImage,Byte3DImage,NNInterpolatorType>(io,output_f,xfm_f,like_f,invert,uniformize,normalize,history.c_str(),store_float,store_short,store_byte,interpolator);
          else if(store_short)
            resample_image<Int3DImage,Short3DImage,NNInterpolatorType>(io,output_f,xfm_f,like_f,invert,uniformize,normalize,history.c_str(),store_float,store_short,store_byte,interpolator);
          else
            resample_image<Int3DImage,Int3DImage,NNInterpolatorType>(io,output_f,xfm_f,like_f,invert,uniformize,normalize,history.c_str(),store_float,store_short,store_byte,interpolator);
        } else { //using slow algorithm
          InterpolatorType::Pointer interpolator = InterpolatorType::New();
          interpolator->SetSplineOrder(order);
          
          if(store_byte)
            resample_label_image<Int3DImage,Float3DImage,Byte3DImage,InterpolatorType>(io,output_f,xfm_f,like_f,invert,uniformize,normalize,history.c_str(),store_float,store_short,store_byte,interpolator);
          else if(store_short)
            resample_label_image<Int3DImage,Float3DImage,Short3DImage,InterpolatorType>(io,output_f,xfm_f,like_f,invert,uniformize,normalize,history.c_str(),store_float,store_short,store_byte,interpolator);
          else
            resample_label_image<Int3DImage,Float3DImage,Int3DImage,InterpolatorType>(io,output_f,xfm_f,like_f,invert,uniformize,normalize,history.c_str(),store_float,store_short,store_byte,interpolator);
        }
      } else {
        InterpolatorType::Pointer interpolator = InterpolatorType::New();
        interpolator->SetSplineOrder(order);
        resample_image<Float3DImage,Float3DImage,InterpolatorType>(io,output_f,xfm_f,like_f,invert,uniformize,normalize,history.c_str(),store_float,store_short,store_byte,interpolator);
      }
     } else if( nd==3 && nc==3 )  { // deal with this case as vector image 
//        VectorInterpolatorType::Pointer interpolator = VectorInterpolatorType::New();
//        interpolator->SetSplineOrder(order);
//        resample_vector_image<Vector3DImage,Vector3DImage,VectorInterpolatorType>(io,output_f,xfm_f,like_f,invert,uniformize,normalize,history.c_str(),store_float,store_short,store_byte,interpolator);
       throw itk::ExceptionObject("This number of dimensions is not supported currently");
    } else {
      throw itk::ExceptionObject("This number of dimensions is not supported currently");
    }
    return 0;
  } 
  catch( itk::ExceptionObject & err )
  {
    std::cerr << "ExceptionObject caught !" << std::endl;
    std::cerr << err << std::endl;
    return 2;
  }
	return 0;
};
