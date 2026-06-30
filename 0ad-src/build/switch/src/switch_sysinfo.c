#include <switch.h>
#include <stdio.h>

int switchGetFirmwareVersion(char* out, unsigned long outSize)
{
	if( !out || outSize == 0 )
		return -1;
	out[0] = '\0';

	if( R_FAILED( setsysInitialize() ) )
		return -1;
	SetSysFirmwareVersion fw;
	Result rc = setsysGetFirmwareVersion( &fw );
	setsysExit();
	if( R_FAILED( rc ) )
		return -1;

	snprintf( out, outSize, "%s", fw.display_version );
	return 0;
}

int switchGetAtmosphereVersion(char* out, unsigned long outSize)
{
	if( !out || outSize == 0 )
		return -1;
	out[0] = '\0';

	if( R_FAILED( splInitialize() ) )
		return -1;
	u64 ver = 0;
	Result rc = splGetConfig( (SplConfigItem)65000, &ver );
	splExit();
	if( R_FAILED( rc ) || ver == 0 )
		return -1;

	unsigned major = (unsigned)( ( ver >> 56 ) & 0xFF );
	unsigned minor = (unsigned)( ( ver >> 48 ) & 0xFF );
	unsigned micro = (unsigned)( ( ver >> 40 ) & 0xFF );
	snprintf( out, outSize, "%u.%u.%u", major, minor, micro );
	return 0;
}
