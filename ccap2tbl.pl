#!/usr/bin/perl

use strict;
use Getopt::Long qw(:config no_ignore_case );

my $fieldname="FIRST_FIPS";
my $inputshape;
my $keep_clip=0;
my $year1 =  0;
my $year2 = 0;
my $subdir = ".";
my $help = 0;

our $featuretype;

my @tmpfiles;

GetOptions (
	"f|fieldname=s" => \$fieldname,
	"s|shapefile=s" => \$inputshape,
	"1|year1=i" => \$year1,
	"2|year2=i" => \$year2,
	"d|subdir=s" => \$subdir,
	"keep_clip" => \$keep_clip,
	"h|help" => \$help,
	);

if ($help){
	usage();
	exit 0;
}

$subdir = '.' if $subdir =~ /current directory/; #in case anyone is that silly

if ($year1 == 0 || $year2 == 0){
	print STDERR "Must provide year 1 and year 2\n";
	usage();
	exit 0;
}

print "Year1, Year2, FeatureID, ClassID, Pixels\n";

if (scalar(@ARGV) == 0){
	print STDERR "Missing input images\n";
	usage();
	exit 1;
}

if (! -e $subdir){
	my $cmd = "mkdir -p $subdir";
	my $rc = system($cmd);
	if ($rc != 0){
		print STDERR "Failed to make directory $subdir for image clips\n";
		exit 1;
	}
}

if (! -w $subdir){
	die "Can not write to directory $subdir for image clips\n";
}

# get the list of input images
my $inputfiles = join(" ",@ARGV);

# get a list of all the features for the shapefile
my %features = get_features_hash($inputshape, $fieldname);

#print_features_list(%features);


# cycle over the features and make the individual shapefiles
my $delim = "'" if $featuretype = "string";


foreach my $featureid (keys %features){
	#my $tmpshp = "/var/tmp/tmp_shp$$.shp"
	
	#my $cmd = "ogr2ogr -f \"ESRI Shapefile\" -overwrite -where \"$fieldname=$delim$featureid$delim\" $inputshape $tmpshp";
	my $outname="$subdir/clipped_${year1}_${year2}_$featureid.img";
	if ( ! -e $outname){
		
		$outname =~ s/\s+/_/; #change spaces to underscores
		push(@tmpfiles,$outname,"$outname.aux.xml") unless $keep_clip;
		my $cmd = "gdalwarp -of HFA -cutline $inputshape -cwhere \"$fieldname=$delim$featureid$delim\" -crop_to_cutline  -co STATISTICS=TRUE -srcnodata 0 -dstnodata 0 -r near -co COMPRESSED=TRUE $inputfiles $outname > /dev/null 2>&1";
		my $rc = system($cmd);
		if ($rc != 0){
			cleanup(@tmpfiles);
			die "CMD = $cmd:\nFailed to make clip for $fieldname = $featureid. Return code = $rc";
		}
	}
	

	# now we have the iamge for just one area. 
	my @ccap = summarize_area($outname);
	for my $ccap (@ccap){
		$ccap =~ s/\s+$//; # dump trailing space or newline
		print "$year1, $year2, $featureid, $ccap\n";
	}

	unlink($outname,"$outname.aux.xml") unless $keep_clip; #unlink now if we can
	
}

cleanup(@tmpfiles);


sub cleanup {
	unlink(@_);
}

sub summarize_area {
	my ($image) = @_;
	open(PIPE, "ccap_summarize $image |") || die "Can't open ccap_summarize";
	my @values = <PIPE>;
	close(PIPE);
	return @values;
}

sub get_features_hash {
	my ($inputshape, $fieldname) = @_;
	my %features = ();
	my $cmd = "ogrinfo -al -geom=SUMMARY $inputshape";
	open(INFO,"$cmd |") || die "Failed to open $cmd";
	while(<INFO>){
		my $line = $_;
		#print "ccap $line";
		if ($line =~ /^  $fieldname /){
			$featuretype = "string" if $fieldname =~ /\(String\)/;
			my ($first,$last) = split(/ = /,$line);
			$last =~ s/\s+$//;
			next if $last =~ /\(null\)/; #if the feature had no name, we can't use it. Skip.
			next if $last =~ /^$/;
			# we're going to use this in filenames and then call on command line. Need to clean!
			# only allow alphanumerics and related
			next unless $last =~ m/([\w-]+)/;
			$last = $1;
			$features{$last} = 1;
			#print "features{$last}\n";
		}
	}
	close(INFO);
	return %features
}

sub print_features_list {
	my (%features) = @_;
	foreach my $key (keys %features){
		print "$key\n";
	}
}

sub usage {
	print STDERR "$0 - make summary tables from CCAP bivariate files\n";
	print STDERR "USAGE: $0 -1|-year1 year1 -2|-year2 year2 -s|-shapefile shapefile [-f|-fieldname fieldname] [-d|-subdir directory] imagefiles ...\n";
	print STDERR "\tyear1 = start year. Just gets printed in a column\n";
	print STDERR "\tyear2 = end year. Just gets printed in a column\n";
	print STDERR "\tshapefile = Shapefile containing the features to summarize by\n";
	print STDERR "\tfieldname = Name of the field to use in the shapefile attribute table [FIRST_FIPS]\n";
	print STDERR "\tsubdir = directory to store the image clips [. or current directory]\n";
	print STDERR "\t-keep_clip = do not delete the image clips when done\n";
	print STDERR "\timagefiles = input bivariate CCAP images.\n";
	print STDERR "Output is a comma separated value table with number of pixels in each class for each feature\n";
}