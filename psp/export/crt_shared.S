	.set noreorder

#include "pspstub.s"

	STUB_START "crt_shared",0x00090000,0x00270005
	STUB_FUNC  0x411B4582,abs
	STUB_FUNC  0x60054052,rand
	STUB_FUNC  0x1B00FB95,qsort
	STUB_FUNC  0xAD8AF84F,free
	STUB_FUNC  0x0FEA4E71,time
	STUB_FUNC  0xD7CC5BF2,localtime
	STUB_FUNC  0xEB6644C2,vsnprintf
	STUB_FUNC  0x54F3C563,vsprintf
	STUB_FUNC  0x7661E728,sprintf
	STUB_FUNC  0x27F9052C,sscanf
	STUB_FUNC  0xCAB439DF,printf
	STUB_FUNC  0x481C9ADA,malloc
	STUB_FUNC  0x10F3BB61,memset
	STUB_FUNC  0xAB7592FF,memcpy
	STUB_FUNC  0x81D0D1F7,memcmp
	STUB_FUNC  0xA48D2592,memmove
	STUB_FUNC  0xB8A55300,atoi
	STUB_FUNC  0x3F381760,atof
	STUB_FUNC  0x1A36A2FC,stpcpy
	STUB_FUNC  0xBA4268E8,strdup
	STUB_FUNC  0xEC6F1CF2,strcpy
	STUB_FUNC  0xB49A7697,strncpy
	STUB_FUNC  0x52DF196C,strlen
	STUB_FUNC  0x0D188658,strstr
	STUB_FUNC  0x476FD94A,strcat
	STUB_FUNC  0xC0AB8932,strcmp
	STUB_FUNC  0xB1DC2AE8,strchr
	STUB_FUNC  0x87F8D2DA,strtok
	STUB_FUNC  0x7AB35214,strncmp
	STUB_FUNC  0x3EB35691,strcasecmp
	STUB_FUNC  0x0CF9A48C,__errno
	STUB_FUNC  0xC7B65D19,fabs
	STUB_FUNC  0x3C15F372,sqrt
	STUB_FUNC  0x4AC0147D,sin
	STUB_FUNC  0x4D2CCC5B,cos
	STUB_FUNC  0xF18CCA35,tan
	STUB_FUNC  0x7E35A7B1,atan2
	STUB_FUNC  0x0CF83A61,ceil
	STUB_FUNC  0xD3C98436,floor
	STUB_END
