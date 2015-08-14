#include <stdio.h>
#include <strings.h>
#include "gdal.h"
#include "gdal_priv.h"
#include "gdal_rat.h"
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
GDALColorTable * makeColorTable(char *psFilename);
void printRGB(const GDALColorEntry *color);
char **getHFAOptions();
char **getTiffOptions();
void printColorTable(GDALColorTable *poColorTable);

void usage(char *name){
	fprintf(stderr,"%s - calculate the bivariate CCAP file from the single date files\n",name);
	fprintf(stderr,"USAGE: %s -c colorfile -s start_ccap -e end_ccap -o bivariate_file\n",name);
	fprintf(stderr,"\tcolorfile = 4 column space separated color file for bivariate (index red green blue)\n");
	fprintf(stderr,"\tstart_ccap = C-CAP file with first year of data\n");
	fprintf(stderr,"\tend_ccap = C-CAP file with final year of data\n");
	fprintf(stderr,"\tbivariate_file = C-CAP bivariate output file\n");

}
int main(int argc, char **argv)
{

	
	
	int c, i, j;
	char *shpname = NULL;
	char *fieldname = NULL;
	char *psColorTable = NULL;
	FILE *tfp = stdout;
	int verbose = 0;
	GDALDataset *poStartCCAP = NULL;
	GDALDataset *poEndCCAP = NULL;
	GDALDataset *poBivariate = NULL;
	char *psBivariateName = NULL;
	unsigned int *histogram = NULL;


	extern int optind;
	extern char *optarg;

	//const char *pszFormat = "HFA";
	char gdalformat[10];
	GDALDriver *poDriver;
	char **papszMetadata;

	GDALAllRegister();
	OGRRegisterAll();

	
	

	

	while((c = getopt(argc,argv,"c:s:e:o:vh")) != -1){
		switch(c){
			case 'c':
				psColorTable = optarg; // file name for a colortable (3 column)
				break;
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

	// figure out the format for the output
	if(psBivariateName == NULL){
		fprintf(stderr,"Must supply output file name\n");
		usage(argv[0]);
		return 1;
	}
	char **papszOptions = NULL;
	
	const char * psLastPeriod = strrchr(psBivariateName,'.');
	if(strcasecmp(psLastPeriod,".img") == 0){
		strcpy(gdalformat,"HFA");
		papszOptions = getHFAOptions();
	}else if(strcasecmp(psLastPeriod,".tif") == 0){
		strcpy(gdalformat,"GTiff");
		papszOptions = getTiffOptions();
	}else{
		fprintf(stderr,"Not supported output format yet\n");
		return 1;
	}

	// validate that we can use create
	poDriver = GetGDALDriverManager()->GetDriverByName(gdalformat);

  if( poDriver == NULL )
      return 1;

  papszMetadata = poDriver->GetMetadata();
  if( ! CSLFetchBoolean( papszMetadata, GDAL_DCAP_CREATE, FALSE ) ) {
  	printf( "Driver %s does not support Create() method.\n", gdalformat );
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
	
	if( (poBivariate = poDriver->Create(psBivariateName, nXSize, nYSize, 1,
		GDT_UInt16,papszOptions)) == NULL){
		fprintf(stderr,"Failed to created output file %s\n",psBivariateName);
		return 1;
	}

	// add georeferencing and such
	double adfGeoTransform[6];
	if(poEndCCAP->GetGeoTransform( adfGeoTransform ) == CE_None){
		poBivariate->SetGeoTransform(adfGeoTransform);
	}else{
		fprintf(stderr,"Warning: End date C-CAP file missing georeferencing\n");
	}

	poBivariate->SetProjection(poEndCCAP->GetProjectionRef());

	
	
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

	// look at what a color table looks like
	//printColorTable(poBandEnd->GetColorTable());


	if(poBandStart == NULL || poBandEnd == NULL || poBandOut == NULL){
		fprintf(stderr,"Failed to get one of the bands!\n");
		GDALExit(1);
		return 1;
	}

	// initialize the color table for bivariate if we can.
	GDALDefaultRasterAttributeTable *poRAT = NULL;
	GDALColorTable *poColorTable = NULL;
	if(psColorTable){
		poRAT = new GDALDefaultRasterAttributeTable();
		poColorTable = makeColorTable(psColorTable);
			if(poColorTable != NULL){
			poRAT->InitializeFromColorTable(poColorTable);
			// add fields for histogram
			poRAT->CreateColumn("Histogram", GFT_Integer, GFU_PixelCount);
			poBandOut->SetColorTable(poColorTable);
			poBandOut->SetDefaultRAT(poRAT);
		}else{
			fprintf(stderr, "Failed to make the color table from file!\n");
			delete poRAT;
			poRAT = NULL;
		}
	}
	// allocate space for the histogram.
	GUIntBig *anHistogram = (GUIntBig *)CPLMalloc(sizeof(GUIntBig) * (CCAP_CLASSES * CCAP_CLASSES + 1));
	for (int i = 0; i < CCAP_CLASSES * CCAP_CLASSES; i++){ anHistogram[i] = 0;}

	// loop over all the rows and output the bivariate.
	// bivariate value = total_classes * (date1_class -1) + date2_class
	// if either date entry is zero, the answer is zero.
	for(int y = 0; y < nYSize; y++){
		if(poBandStart->RasterIO( GF_Read, 0, y, nXSize, 1, pasScanlineStart, nXSize, 1, GDT_Byte, 0, 0 ) != CE_None){
			fprintf(stderr,"Failed to read the start date data for row %d\n",y);
			GDALExit(1);
		}
		if(poBandEnd->RasterIO( GF_Read, 0, y, nXSize, 1, pasScanlineEnd, nXSize, 1, GDT_Byte, 0, 0 ) != CE_None){
			fprintf(stderr,"Failed to read the end date data for row %d\n",y);
			GDALExit(1);
		}
		// data read in. Now handle each pixel.
		for(int x = 0; x < nXSize; x++){
			unsigned short spix = pasScanlineStart[x];
			unsigned short epix = pasScanlineEnd[x];
			unsigned short nclass;
			nclass = spix && epix ? nclass = CCAP_CLASSES * (spix - 1) + (epix) : 0;
			pasScanlineOut[x] = nclass;
			if(nclass <= CCAP_CLASSES * CCAP_CLASSES)  anHistogram[nclass]++;
		}

		// write out the new line
		if(poBandOut->RasterIO(GF_Write, 0,y,nXSize,1,pasScanlineOut,nXSize,1,GDT_UInt16, 0, 0) != CE_None){
			fprintf(stderr,"Failed to write row %d to output\n",y);
			GDALExit(1);
		}
	}

	// set the historgram values in the RAT. Note RAT is ints and we could overflow
	if(poRAT != NULL){
		int histcol = poRAT->GetColOfUsage(GFU_PixelCount);
		for (int i = 0; i < CCAP_CLASSES * CCAP_CLASSES; i++){
			//fprintf(stderr,"Histogram %d => %ld\n",i,anHistogram[i]);
			poRAT->SetValue(i,histcol,(int)anHistogram[i]) ;
		}

		poRAT->DumpReadable(NULL);
		//if(! poRAT->ChangesAreWrittenToFile()) { poBandOut->SetDefaultRAT(poRAT); }
	}
	
	// check the color table
	/*GDALColorTable *poTestColor = poBandOut->GetColorTable();
	//if (! poTestColor->IsSame(poColorTable)){
		fprintf(stderr,"Color table comparison\n");
		for(int i = 0; i < poTestColor->GetColorEntryCount(); i++){
			fprintf(stderr,"%d: ", i);
			printRGB(poTestColor->GetColorEntry(i));
			fprintf(stderr," vs ");
			printRGB(poColorTable->GetColorEntry(i));
			fprintf(stderr,"\n");
		}*/
	//}


	GDALFlushCache( (GDALDatasetH)poBivariate );

	// All done. Close properly
	GDALClose((GDALDatasetH) poBivariate);
	GDALClose((GDALDatasetH) poEndCCAP);
	GDALClose((GDALDatasetH) poStartCCAP);



	// and deallocate stuff
	CPLFree(pasScanlineStart);
	CPLFree(pasScanlineOut);
	CPLFree(pasScanlineEnd);
	CPLFree(anHistogram);

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
	if((fp = fopen(psFilename,"r")) == NULL){
		fprintf(stderr,"Failed to open colortable file %s\n",psFilename);
		return NULL;
	}
	
	while(fscanf(fp, "%d %hu %hu %hu",&index, &color.c1,&color.c2,&color.c3) == 4){
		color.c4 = index ? 1 : 0; // index 0 should be transparent for CCAP bivariate.
		//color.c1 <<= 8;
		//color.c2 <<= 8;
		//color.c3 <<= 8;
		poColorTable->SetColorEntry(index, &color);
	}
	fclose(fp);

	return poColorTable;
}

void printRGB(const GDALColorEntry *color)
{
	fprintf(stderr,"%d %d %d %d",color->c1, color->c2, color->c3, color->c4);
}

void printColorTable(GDALColorTable *poColorTable)
{
	for( int i = 0; i < poColorTable->GetColorEntryCount(); i++){
		fprintf (stderr,"%d: ",i);
		printRGB(poColorTable->GetColorEntry(i));
		fprintf (stderr,"\n");
	}
}

char **getHFAOptions()
{
	char **papszOptions = NULL;
	 papszOptions = CSLSetNameValue(papszOptions,"STATISTICS","TRUE");
	 //papszOptions = CSLSetNameValue(papszOptions,"AUX", "TRUE");
	 papszOptions = CSLSetNameValue(papszOptions,"COMPRESSED", "TRUE");

	 return papszOptions;
}

char **getTiffOptions()
{
	char **papszOptions = NULL;
	 papszOptions = CSLSetNameValue(papszOptions,"COMPRESS", "PACKBITS");

	 return papszOptions;
}
