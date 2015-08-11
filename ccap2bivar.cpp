#include <stdio.h>
#include <strings.h>
#include "gdal.h"
#include "gdal_priv.h"
#include "gdal_alg.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "ogr_spatialref.h"
#include "ogrsf_frmts.h"
#include "ogr_api.h"
//#include "commonutils.h"
//#include <vector>
//#include <map>

#define CCAP_CLASSES 25


static int GDALExit( int nCode );

void usage(char *name){
	fprintf(stderr,"%s - calculate the bivariate CCAP file from the single date files\n",name);
	fprintf(stderr,"USAGE: %s -s start_ccap -e end_ccap -o bivariate_file\n",name);
	fprintf(stderr,"\tstart_ccap = C-CAP file with first year of data\n");
	fprintf(stderr,"\tend_ccap = C-CAP file with final year of data\n");
	fprintf(stderr,"\tbivariate_file = C-CAP bivariate output file\n");

}
int main(int argc, char **argv)
{

	
	
	int c, i, j;
	char *shpname = NULL;
	char *fieldname = NULL;
	FILE *tfp = stdout;
	int verbose = 0;
	GDALDataset *poStartCCAP = NULL;
	GDALDataset *poEndCCAP = NULL;
	GDALDataset *poBivariate = NULL;
	char *psBivariateName = NULL;


	extern int optind;
	extern char *optarg;

	const char *pszFormat = "HFA";
	GDALDriver *poDriver;
	char **papszMetadata;

	GDALAllRegister();
	OGRRegisterAll();

	poDriver = GetGDALDriverManager()->GetDriverByName(pszFormat);

  if( poDriver == NULL )
      exit( 1 );

  papszMetadata = poDriver->GetMetadata();
  if( CSLFetchBoolean( papszMetadata, GDAL_DCAP_CREATE, FALSE ) )
      printf( "Driver %s supports Create() method.\n", pszFormat );
  if( CSLFetchBoolean( papszMetadata, GDAL_DCAP_CREATECOPY, FALSE ) )
      printf( "Driver %s supports CreateCopy() method.\n", pszFormat );
	

	

	while((c = getopt(argc,argv,"s:e:o:vh")) != -1){
		switch(c){
			case 's':
				poStartCCAP = (GDALDataset *)GDALOpen( optarg, GA_ReadOnly );
				if(poStartCCAP == NULL){
					fprintf(stderr,"Failed to open start C-CAP file %s\n",optarg);
					usage(argv[0]);
					return 1;
				}
				break;
			case 'e':
				poEndCCAP = (GDALDataset *)GDALOpen( optarg, GA_ReadOnly );
				if(poEndCCAP == NULL){
					fprintf(stderr,"Failed to open end C-CAP file %s\n",optarg);
					usage(argv[0]);
					return 1;
				}
				break;
			case 'o':
				psBivariateName = optarg;
				break;
			case 'v':
				verbose++;
				break;
			case 'h':
				usage(argv[0]);
				return 0;
			default:
				fprintf(stderr,"unknown option -%c\n",c);
				usage(argv[0]);
				return 1;
		}
	}

	if (poEndCCAP == NULL || poStartCCAP == NULL){
		fprintf(stderr,"Must supply start and end C-CAP files\n");
		usage(argv[0]);
		return 1;
	}

	// Validate that the input rasters are the same size, etc. 
	if(poEndCCAP->GetRasterXSize() != poStartCCAP->GetRasterXSize() 
		|| poEndCCAP->GetRasterYSize() != poStartCCAP->GetRasterYSize()){
		fprintf(stderr,"Input files are not the same size!\n");
		return 1;
	}

	int nXSize = poEndCCAP->GetRasterXSize();
	int nYSize = poEndCCAP->GetRasterYSize();
	/**
	* Want to create an output file the same size as the input files,
	* but with 16 bit unsigned instead of 8 bit.
	*/
	char **papszOptions = NULL;
	poBivariate = poDriver->Create(psBivariateName, nXSize, nYSize, 1,
		GDT_UInt16,papszOptions);

	// add georeferencing and such
	double adfGeoTransform[6];
	if(poEndCCAP->GetGeoTransform( adfGeoTransform ) == CE_None){
		poBivariate->SetGeoTransform(adfGeoTransform);
	}else{
		fprintf(stderr,"Warning: End date C-CAP file missing georeferencing\n");
	}

	poBivariate->SetProjection(poEndCCAP->GetProjectionRef());

	// initialize the color table for bivariate if we can.
	if(psColorTable){
		GDALColorTable *poColorTable = makeColorTable(psColorTable);
		GDALRasterAttributeTable rat;
		rat.InitializeFromColorTable(poColorTable);
	}

	// allocate for a line at a time
	unsigned char *pasScanlineStart;
	unsigned char *pasScanlineEnd;
	unsigned short *pasScanlineOut;
	pasScanlineStart = (unsigned char *)CPLMalloc(sizeof(unsigned char)*nXSize);
	pasScanlineEnd = (unsigned char *)CPLMalloc(sizeof(unsigned char)*nXSize);
	pasScanlineOut = (unsigned short *)CPLMalloc(sizeof(unsigned short)*nXSize);
	if(pasScanlineStart == NULL || pasScanlineEnd == NULL || pasScanlineOut == NULL){
		fprintf(stderr,"Failed to allocate scanlines for %d width\n",nXSize);
		GDALExit(1);
		return 1;
	}

	// get the band info
	GDALRasterBand *poBandStart = poStartCCAP->GetRasterBand( 1 );
	GDALRasterBand *poBandEnd = poEndCCAP->GetRasterBand( 1 );
	GDALRasterBand *poBandOut = poBivariate->GetRasterBand( 1 );

	// loop over all the rows and output the bivariate.
	// bivariate value = total_classes * (date1_class -1) + date2_class
	// if either date entry is zero, the answer is zero.
	for(int y = 0; y < nYSize; y++){
		poBandStart->RasterIO( GF_Read, 0, y, nXSize, 1, pasScanlineStart, nXSize, 1, GDT_Byte, 0, 0 );
		poBandEnd->RasterIO( GF_Read, 0, y, nXSize, 1, pasScanlineEnd, nXSize, 1, GDT_Byte, 0, 0 );
		// data read in. Now handle each pixel.
		for(int x = 0; x < nXSize; x++){
			unsigned short spix = pasScanlineStart[x];
			unsigned short epix = pasScanlineEnd[x];
			if (spix == 0 || epix == 0){
				pasScanlineOut[x] = 0;
			}else{
				pasScanlineOut[x] = CCAP_CLASSES * (spix - 1) + (epix);
			}
		}

		// write out the new line
		poBandOut->RasterIO(GF_Write, 0,y,nXSize,1,pasScanlineOut,nXSize,1,GDT_UInt16, 0, 0);
	}

	// All done. Close properly
	GDALClose((GDALDatasetH) poBivariate);
	GDALClose((GDALDatasetH) poEndCCAP);
	GDALClose((GDALDatasetH) poStartCCAP);

	// and deallocate stuff
	CPLFree(pasScanlineStart);
	CPLFree(pasScanlineOut);
	CPLFree(pasScanlineEnd);

	GDALExit(0);

}

	

/************************************************************************/
/*                               GDALExit()                             */
/*  This function exits and cleans up GDAL and OGR resources            */
/*  Perhaps it should be added to C api and used in all apps?   
/* copied from gdalwarp.cpp        */
/************************************************************************/

static int GDALExit( int nCode )
{
  const char  *pszDebug = CPLGetConfigOption("CPL_DEBUG",NULL);
  if( pszDebug && (EQUAL(pszDebug,"ON") || EQUAL(pszDebug,"") ) )
  {  
    GDALDumpOpenDatasets( stderr );
    CPLDumpSharedList( NULL );
  }

  GDALDestroyDriverManager();

#ifdef OGR_ENABLED
  OGRCleanupAll();
#endif

  exit( nCode );
}

GDALColorTable * makeColorTable(char *psFilename)
{
	FILE *fp;
	GDALColorEntry color;
	GDALColorTable *poColorTable = new GDALColorTable;
	int index;
	if((fp == fopen(psFilename,"r")) == NULL){
		fprintf(stderr,"Failed to open colortable file %s\n",psFilename);
		return NULL;
	}
	color.c4 = 1;
	while(fscanf("%d %hu %hu %hu",&index, &color.c1,&color.c2,&color.c3) == 4){
		poColorTable->setColorEntry(index, &color);
	}
	fclose(fp);

	return poColorTable;
}