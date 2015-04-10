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
#include <vector>
#include <map>

#define CCAP_CLASSES 625


static void
TransformCutlineToSource( GDALDataset * poDS, OGRFeature *poCutline,
													OGRGeometry ** ppoMultiPolygon,
                           char **papszTO_In );
void usage(char *name){
	fprintf(stderr,"%s - calculate table from bivariate CCAP file\n",name);
	fprintf(stderr,"USAGE: %s -1 year1 -2 year2 -s shapefile -f fieldname [-t table] bivariate_file\n",name);
	fprintf(stderr,"\tyear1 = early year of the bivariate file (eg 1996)\n");
	fprintf(stderr,"\tyear2 = late year of the bivariate file (eg 2010)\n");
	fprintf(stderr,"\tshapefile = vector file of features to tabulate by (eg counties)\n");
	fprintf(stderr,"\tfieldname = field name in the vector attributes to outut for each feature (eg FIPS)\n");
	fprintf(stderr,"\ttable = output file for table [stdout]");
	fprintf(stderr,"\tbivariate_file = C-CAP bivariate file to analyze\n");

}
int main(int argc, char **argv)
{

	
	
	int c, i, j;
	int year1 = 0, year2 = 0;
	char *shpname = NULL;
	char *fieldname = NULL;
	FILE *tfp = stdout;
	unsigned long long *table;
	int verbose = 0;
	GDALDataset *poVDS; // vector data set.
	OGRDataSourceH hSrcDS; // vector data set.
	char                **papszTO = NULL; /* options in OPTION=VALUE format, probably not used */
	


	extern int optind;
	extern char *optarg;
	
	table = (unsigned long long *)calloc(sizeof(unsigned long long)*(CCAP_CLASSES+1), sizeof(unsigned long long));

	GDALAllRegister();

	while((c = getopt(argc,argv,"1:2:t:s:vf:h")) != -1){
		switch(c){
			case '1':
				year1 = atoi(optarg);
				break;
			case '2':
				year2 = atoi(optarg);
				break;
			case 's':
				shpname = optarg;
				break;
			case 't':
				if((tfp = fopen(optarg,"w")) == NULL){
					fprintf(stderr,"Failed to open '%s' for output\n",optarg);
					usage(argv[0]);
					return 1;
				}
				break;
			case 'f':
				fieldname = optarg;
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

	if(optind == argc){
		// no more args, but don't have the bivariate file!
		fprintf(stderr,"Missing bivariate file name\n");
		usage(argv[0]);
		return 1;
	}
	if(!year1 || !year2){
		fprintf(stderr,"C'mon man, read the help. You've got to give me the start and end years\n");
		usage(argv[0]);
		return 1;
	}
	if(fieldname == NULL){
		fprintf(stderr,"Missing fieldname of shapefile attribute to use\n");
		usage(argv[0]);
		return 1;
	}
	if(shpname == NULL){
		fprintf(stderr,"Missing the shapefile\n");
		usage(argv[0]);
		return 1;
	}else{

		/* example from the OGR API tutorial, but it doesn't work. There is not GDALOpenEx. Looks like a version 2.0 new thing
		poVDS = (GDALDataset*)GDALOpenEx( shpname, GDAL_OF_VECTOR, NULL, NULL, NULL ); // stolen from the OGR API tutorial
		if(poVDS == NULL){
			fprintf(stderr,"Failed to open vector file %s\n",shpname);
			return 1;
		}
		*/
		/* -------------------------------------------------------------------- */
		/*      Open source vector dataset.                                     */
		/* -------------------------------------------------------------------- */
    //OGRDataSourceH hSrcDS;

		// not really sure this recast is right. Can OGRDataSourceH be cast to GDALDataset?
    hSrcDS = OGROpen( shpname, FALSE, NULL );
    if( hSrcDS == NULL ){
      fprintf(stderr,"Failed to open vector file %s\n",shpname);
			return 1;
		}
	}

	// print the table header
	// Dump the table
	fprintf(tfp,"Year1, Year2, FeatureID, classID, Pixels\n");

	// open all the raster datasets after allocating some space for them
	int nrasters = argc-optind;
	GDALDataset ** poDataset = (GDALDataset **)CPLMalloc(sizeof(GDALDataset *) * nrasters);
	GDALRasterBand ** poBand = (GDALRasterBand **)CPLMalloc(sizeof(GDALRasterBand *) * nrasters);
	int *nXSize = (int *)CPLMalloc(sizeof(int) * nrasters);
	int *nYSize = (int *)CPLMalloc(sizeof(int) * nrasters);

	double        adfGeoTransform[6];
	int maxX = 0;
	for(i = 0, j=optind; i < nrasters; i++, j++){
		poDataset[i] = (GDALDataset *)GDALOpen( argv[j], GA_ReadOnly );
		if( poDataset[i] == NULL ){
	  	fprintf(stderr,"Failed to open file %s .. skipping\n", argv[j]);
	  	i--;
	  	nrasters--;
	  	continue;
	  }else{
	  	fprintf(stderr,"Working on file %s\n",argv[j]);
	  }

	  if(verbose){
    	printf( "Driver: %s/%s\n",
            poDataset[i]->GetDriver()->GetDescription(), 
            poDataset[i]->GetDriver()->GetMetadataItem( GDAL_DMD_LONGNAME ) );

    	printf( "Size is %dx%dx%d\n", 
            poDataset[i]->GetRasterXSize(), poDataset[i]->GetRasterYSize(),
            poDataset[i]->GetRasterCount() );
    }
    

    if( poDataset[i]->GetProjectionRef()  != NULL && verbose )
        printf( "Projection is `%s'\n", poDataset[i]->GetProjectionRef() );

    if( poDataset[i]->GetGeoTransform( adfGeoTransform ) == CE_None && verbose)
    {
        printf( "Origin = (%.6f,%.6f)\n",
                adfGeoTransform[0], adfGeoTransform[3] );

        printf( "Pixel Size = (%.6f,%.6f)\n",
                adfGeoTransform[1], adfGeoTransform[5] );
    }

    poBand[i] = poDataset[i]->GetRasterBand( 1 );
    nXSize[i] =  poBand[i]->GetXSize();
  	nYSize[i] = poBand[i]->GetYSize();
  	maxX = nXSize[i] > maxX ? nXSize[i] : maxX; 
  }

  // space for the scanline
	unsigned short *pasScanline;
	pasScanline = (unsigned short *)CPLMalloc(sizeof(unsigned short)*maxX);


	// open the vector layer and run through the features
	// for each feature, we'll pull out the chuncks of raster needed
	OGRLayer *poLayer;

	poLayer = (OGRLayer *)OGR_DS_GetLayer(hSrcDS,0); // Get the first (only) layer


	
	/*
	* cycle over all features
	*/
	
	OGRFeature *poFeature;
	OGRGeometry *poMultiPolygon;
	OGREnvelope sEnvelope;
	OGRPoint pPoint;
	poLayer->ResetReading();
	while((poFeature = poLayer->GetNextFeature()) != NULL){
		/* get the value of our attibute, probably the county id or some such */
		OGRFeatureDefn *poFDefn = poLayer->GetLayerDefn();
		int iField = poFeature->GetFieldIndex(fieldname);
		if(iField == -1){
			fprintf(stderr,"Failed to find field %s\n",fieldname);
			return 1;
		}
    const char *featureVal = (const char *)poFeature->GetFieldAsString(iField);

    
    /*
		* To Do:
		* For each raster:
		*   Transform the feature such that it is in pixel/line coordinates
		*   pull out subsets of the raster line by line that are in the feature
		*   Test each point to see if in polygon. Operate on those that are
		*/
    for(i = 0; i < nrasters; i++){

	    // read the band line by line

	    /* -------------------------------------------------------------------- */
			/*      If we have a cutline, transform it into the source              */
			/*      pixel/line coordinate system and insert into warp options.      */
			/*      This gives a WKT version of the feature in the papszWarpOptions */
			/*      under the CUTLINE field                                         */
			/* -------------------------------------------------------------------- */
      
      TransformCutlineToSource( poDataset[i], poFeature,
      													&poMultiPolygon, 
                               
                                papszTO );
      

      /*****************
      /* options:
      /* 1) make an image mask of the cut area with GDALWarpCutlineMasker
      /* 2) extract area needed and then cut???
      */
	   
      poMultiPolygon->getEnvelope(&sEnvelope);
      int cMaxX = (int)ceil(sEnvelope.MaxX);
      int fMinX = (int)floor(sEnvelope.MinX);
      int cMaxY = (int)ceil(sEnvelope.MaxY);
      int fMinY = (int)floor(sEnvelope.MinY);


	    int x,y; // index for pixels
	    int ystart = fMinY > 0 ? fMinY : 0;
	    int yend = cMaxY >= nYSize[i] ? nYSize[i] : cMaxY + 1 ;
	    int xmin = fMinX > 0 ? fMinX : 0;
	    int xmax = cMaxX >= nXSize[i] ? nXSize[i] : cMaxX +1 ;
	    for(y = ystart; y < yend; y++){
	    	int xwidth = xmax - xmin;
	    	poBand[i]->RasterIO( GF_Read, xmin, y, xwidth, 1, 
	                          pasScanline, xwidth, 1, GDT_UInt16, 
	                          0, 0 );
	    	
	    	// now we have a line of data. Run through it and split into pieces
	    	
	    	for(x = 0; x < xwidth; x++){
	    		pPoint.setX(x+xmin+.5);
	    		pPoint.setY(y+.5);

	    		if(pPoint.Within(poMultiPolygon) && pasScanline[x] > 0 && pasScanline[x] <= CCAP_CLASSES){
	    			table[pasScanline[x]]++;
	    		}

	    		
	    	}
	    	

	    }
		}

		// have finished a feature, can dump it out
		for(i = 0; i < CCAP_CLASSES; i++){
			fprintf(tfp,"%d, %d, %s, %d, %llu\n",year1, year2, featureVal, i, table[i]);
			table[i] = 0; // clear for the next feature
		}
	}

	
	

	// Don't forget to close things and free space
  for(i = 0; i < nrasters; i++){
  	GDALClose((GDALDatasetH)poDataset[i]);
  	//poDataset[i]->GDALClose(); // GDAL 2.0 version
  }
  CPLFree(poDataset);
  CPLFree(pasScanline);
  CPLFree(poBand);
  CPLFree(nXSize);
  CPLFree(nYSize);
  free(table);

	
	
	


}

/************************************************************************/
/*                      GeoTransform_Transformer()                      */
/*                                                                      */
/*      Convert points from georef coordinates to pixel/line based      */
/*      on a geotransform.                                              */
/************************************************************************/

class CutlineTransformer : public OGRCoordinateTransformation
{
public:

    void         *hSrcImageTransformer;

    virtual OGRSpatialReference *GetSourceCS() { return NULL; }
    virtual OGRSpatialReference *GetTargetCS() { return NULL; }

    virtual int Transform( int nCount, 
                           double *x, double *y, double *z = NULL ) {
        int nResult;

        int *pabSuccess = (int *) CPLCalloc(sizeof(int),nCount);
        nResult = TransformEx( nCount, x, y, z, pabSuccess );
        CPLFree( pabSuccess );

        return nResult;
    }

    virtual int TransformEx( int nCount, 
                             double *x, double *y, double *z = NULL,
                             int *pabSuccess = NULL ) {
        return GDALGenImgProjTransform( hSrcImageTransformer, TRUE, 
                                        nCount, x, y, z, pabSuccess );
    }
};



/************************************************************************/
/*                      TransformCutlineToSource()                      */
/*                                                                      */
/*      Transform cutline from its SRS to source pixel/line coordinates.*/
/************************************************************************/
static void
TransformCutlineToSource( GDALDataset * poDS, OGRFeature *poCutline,
													OGRGeometry ** ppoMultiPolygon,
                           char **papszTO_In )

{
#ifdef OGR_ENABLED
    OGRGeometry *poMultiPolygon = poCutline->GetGeometryRef()->clone();
    char **papszTO = CSLDuplicate( papszTO_In );

/* -------------------------------------------------------------------- */
/*      Checkout that SRS are the same.                                 */
/* -------------------------------------------------------------------- */
    OGRSpatialReferenceH  hRasterSRS = NULL;
    const char *pszProjection = NULL;

    if( poDS->GetProjectionRef() != NULL 
        && strlen(poDS->GetProjectionRef()) > 0 )
        pszProjection = poDS->GetProjectionRef();
    else if( poDS->GetGCPProjection() != NULL )
        pszProjection = poDS->GetGCPProjection();

    if( pszProjection == NULL || EQUAL( pszProjection, "" ) )
        pszProjection = CSLFetchNameValue( papszTO, "SRC_SRS" );

    if( pszProjection != NULL )
    {
        hRasterSRS = OSRNewSpatialReference(NULL);
        if( OSRImportFromWkt( hRasterSRS, (char **)&pszProjection ) != CE_None )
        {
            OSRDestroySpatialReference(hRasterSRS);
            hRasterSRS = NULL;
        }
    }

    OGRSpatialReference * poCutlineSRS = poMultiPolygon->GetSpatialReference() ;
    if( hRasterSRS != NULL && poCutlineSRS != NULL )
    {
        /* ok, we will reproject */
    }
    else if( hRasterSRS != NULL && poCutlineSRS == NULL )
    {
        fprintf(stderr,
                "Warning : the source raster dataset has a SRS, but the cutline features\n"
                "not.  We assume that the cutline coordinates are expressed in the destination SRS.\n"
                "If not, cutline results may be incorrect.\n");
    }
    else if( hRasterSRS == NULL && poCutlineSRS != NULL )
    {
        fprintf(stderr,
                "Warning : the input vector layer has a SRS, but the source raster dataset does not.\n"
                "Cutline results may be incorrect.\n");
    }

    if( hRasterSRS != NULL )
        OSRDestroySpatialReference(hRasterSRS);

/* -------------------------------------------------------------------- */
/*      Extract the cutline SRS WKT.                                    */
/* -------------------------------------------------------------------- */
    if( poCutlineSRS != NULL )
    {
        char *pszCutlineSRS_WKT = NULL;

        pszCutlineSRS_WKT = poCutlineSRC->exportToWkt(); 
        papszTO = CSLSetNameValue( papszTO, "DST_SRS", pszCutlineSRS_WKT );
        CPLFree( pszCutlineSRS_WKT );
    }

/* -------------------------------------------------------------------- */
/*      It may be unwise to let the mask geometry be re-wrapped by      */
/*      the CENTER_LONG machinery as this can easily screw up world     */
/*      spanning masks and invert the mask topology.                    */
/* -------------------------------------------------------------------- */
    papszTO = CSLSetNameValue( papszTO, "INSERT_CENTER_LONG", "FALSE" );

/* -------------------------------------------------------------------- */
/*      Transform the geometry to pixel/line coordinates.               */
/* -------------------------------------------------------------------- */
    CutlineTransformer oTransformer;

    /* The cutline transformer will *invert* the hSrcImageTransformer */
    /* so it will convert from the cutline SRS to the source pixel/line */
    /* coordinates */
    oTransformer.hSrcImageTransformer = 
        GDALCreateGenImgProjTransformer2( (GDALDatasetH) poDS, NULL, papszTO );

    CSLDestroy( papszTO );

    if( oTransformer.hSrcImageTransformer == NULL )
        GDALExit( 1 );

    OGR_G_Transform( (OGRGeometryH) poMultiPolygon, 
                     (OGRCoordinateTransformationH) &oTransformer );
    ppoMultiPolygon = &poMultiPolygon;
    GDALDestroyGenImgProjTransformer( oTransformer.hSrcImageTransformer );

/* -------------------------------------------------------------------- */
/*      Convert aggregate geometry into WKT.                            */
/* -------------------------------------------------------------------- */
    /*char *pszWKT = NULL;

    OGR_G_ExportToWkt( (OGRGeometryH) poMultiPolygon, &pszWKT );
    OGR_G_DestroyGeometry( (OGRGeometryH) poMultiPolygon );

    *ppapszWarpOptions = CSLSetNameValue( *ppapszWarpOptions, 
                                          "CUTLINE", pszWKT );
    CPLFree( pszWKT );*/
#endif
}



