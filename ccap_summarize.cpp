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

#define CCAP_CLASSES 625


static int GDALExit( int nCode );

void usage(char *name){
	fprintf(stderr,"%s - calculate table from bivariate CCAP file\n",name);
	fprintf(stderr,"USAGE: %s bivariate_files\n",name);
	
	fprintf(stderr,"\tbivariate_files = C-CAP bivariate files to analyze\n");

}
int main(int argc, char **argv)
{

	
	
	int c, i, j;
	int year1 = 0, year2 = 0;
	char *shpname = NULL;
	char *fieldname = NULL;
	FILE *tfp = stdout;
	int verbose = 0;

	extern int optind;
	extern char *optarg;
	

	GDALAllRegister();
	OGRRegisterAll();

	while((c = getopt(argc,argv,"1:2:t:s:vf:h")) != -1){
		switch(c){
			
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

	if(optind == argc){
		// no more args, but don't have the bivariate file!
		fprintf(stderr,"Missing bivariate file name\n");
		usage(argv[0]);
		return 1;
	}
	
	


	// open all the raster datasets after allocating some space for them
	int nrasters = argc-optind;

	verbose && fprintf(stderr,"allocating %d bytes for ccap table\n", sizeof(unsigned long long)*(CCAP_CLASSES+1));
	unsigned long long *table = (unsigned long long *)calloc(sizeof(unsigned long long)*(CCAP_CLASSES+1), sizeof(unsigned long long));
	if(table == NULL){
		fprintf(stderr,"Failed to allocate %d bytes for ccap table\n",sizeof(unsigned long long)*(CCAP_CLASSES+1));
		return 1;
	}else{
		verbose && fprintf(stderr,"Allocation done. table at address %x\n",table);
	}
	
	for(i = 0, j=optind; i < nrasters; i++, j++){
		GDALDataset *poDataset = (GDALDataset *)GDALOpen( argv[j], GA_ReadOnly );
		if( poDataset == NULL ){
	  	fprintf(stderr,"Failed to open file %s .. skipping\n", argv[j]);
	  	i--;
	  	nrasters--;
	  	continue;
	  }else{
	  	verbose && fprintf(stderr,"Working on file %s\n",argv[j]);
	  }

	  if(verbose){
    	printf( "Driver: %s/%s\n",
            poDataset->GetDriver()->GetDescription(), 
            poDataset->GetDriver()->GetMetadataItem( GDAL_DMD_LONGNAME ) );

    	printf( "Size is %dx%dx%d\n", 
            poDataset->GetRasterXSize(), poDataset->GetRasterYSize(),
            poDataset->GetRasterCount() );
    }
    

    

    GDALRasterBand *poBand = poDataset->GetRasterBand( 1 );
    int nXSize =  poBand->GetXSize();
  	int nYSize = poBand->GetYSize();
  	
  	// space for the scanline
		unsigned short *pasScanline;
		verbose && fprintf(stderr,"Allocating for a scanline\n");
		pasScanline = (unsigned short *)CPLMalloc(sizeof(unsigned short)*nXSize);
		if(pasScanline == NULL){
			fprintf(stderr,"Failed to allocated %d bytes for a scanline for file %s\n",sizeof(unsigned short)*nXSize,argv[j]);
		}

		for(int y = 0; y < nYSize; y++){
    	//fprintf(stderr,"\t\tworking on line %d\n",y);
    	
    	poBand->RasterIO( GF_Read, 0, y, nXSize, 1, 
                          pasScanline, nXSize, 1, GDT_UInt16, 
                          0, 0 );
    	
    	// now we have a line of data. Run through it and split into pieces
    	
    	for(int x = 0; x < nXSize; x++){

    		if( pasScanline[x] > 0 && pasScanline[x] <= CCAP_CLASSES){
    			table[pasScanline[x]]++;
    		}
    		
    	}
    	
    }
    delete poDataset;
 	}

 	// done with all rasters, dump out the answers in form Class#, #counted
 	for(i = 1; i <= CCAP_CLASSES; i++){
 		if(table[i] > 0) printf("%d, %ld\n", i, table[i]);
 	}

  
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

