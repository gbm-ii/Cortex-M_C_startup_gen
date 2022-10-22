/*
	Generate the startup module of a Cortex-M microcontroller from the MCU header file
	gbm 10'2022
*/
#include <stdio.h>
#include <string.h>

#define MAXLINELEN	140
#define IRQNAMELEN	32

const char helpstr[] = "Usage: h2cstartup <mcuname>.h\n"
"produces C source file startup_<mcuname>.c containing the complete startup module\n"
"with properly named exception vectors from MCU resource definition header file\n"
"Options:\n"
"-i        define names for unused NVIC interrupts\n"
"-n <irqn> define table with <irqn> IRQ vectors up to <irqn>-1\n"
"-s        use short standard names for core exception handlers\n"
"";

enum error_ {ERR_NOFILE = 2, ERR_BADFORMAT, ERR_BADOPT};

char line[MAXLINELEN];

char irqname[512][IRQNAMELEN];

const char * const stdexcname[] = {
	0, 0,
	"NMI_",
	0,	// HardFault
	"MemManage_",
	0, 0,	// BusFault, UsageFault
	0, 0, 0, 0,
	"SVC_",
	"DebugMon_",
	0,
	0, //"PendSV_"
	0
};

const char heading[] = 
"/*\n"
"    %s\n"
"    gcc-arm compatible C startup module generated by h2cstartup from %s\n"
"    gbm 10'2022\n"
"    https://github.com/gbm-ii/Cortex-M_C_startup_gen \n\n"
;

const char defhandler[] = 
"// the names below represent memory addresses, not real variables\n"
"extern int\n"
"   _sdata,  // start of .data section\n"
"   _edata,  // end of data section\n"
"   _sidata, // start of .data section image in Flash\n"
"   _sbss,   // start of .bss section\n"
"   _ebss,   // end of .bss section\n"
"   _estack; // bottom of stack location\n"
"\n"
"// external functions called during startup\n"
"void SystemInit(void);\n"
"void __libc_init_array(void);\n"
"int main(void);\n"
"\n"
"// code executed after core reset\n"
"__attribute__ ((naked, noreturn)) void Reset_Handler(void)\n"
"{\n"
"   SystemInit();\n"
"   // initialize .data section values from Flash\n"
"   for (int *dptr = &_sdata, *sptr = &_sidata; dptr < &_edata;)\n"
"       *dptr++ = *sptr++;\n"
"   // zero the .bss section\n"
"   for (int *dptr = &_sbss; dptr < &_ebss; dptr++)\n"
"       *dptr = 0;\n"
"   __libc_init_array();\n"
"   main();\n"
"   for (;;);\n"
"}\n"
"\n"
"// the default empty handler for exceptions not handled by user\n"
"static void Default_Handler(void)\n"
"{\n"
"    for (;;);\n"
"}\n\n";

const char vtstart[] = "\n"
"struct vectable_ {\n"
"    void *Initial_SP;\n"
"    void (*Core_Exceptions[15])(void);\n"
"    void (*NVIC_Interrupts[])(void);\n"
"};\n\n"
"#define CX(a) [(a) - 1]\n\n"
"const struct vectable_ g_pfnvectors __attribute__((section(\".isr_vector\"))) = {\n"
"    .Initial_SP = &_estack,\n"
"    .Core_Exceptions = {\n"
"        CX( 1) = Reset_Handler,\n";

const char vtmid[] = 
"    },\n"
"    .NVIC_Interrupts = {\n";

const char vtend[] = "    }\n};\n";

const char *get_stripped_name(const char *name)
{
	const char *stripped_name = strrchr(name, '/');
	if (stripped_name == NULL)
		stripped_name = strrchr(name, '\\');
	if (stripped_name == NULL)
		stripped_name = name;
	else
		stripped_name++;
	return stripped_name;
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf(helpstr);
		return 0;
	}
	
	int carg;
	_Bool addirqs = 0;
	_Bool stdexcnames = 0;
	int nirqns = -1;
	
	for (carg = 1; carg < argc && argv[carg][0] == '-'; carg++)
	{
		switch (argv[carg][1])
		{
		case 'n':
			if (++carg >= argc || sscanf(argv[carg], "%d", &nirqns) != 1)
			{
				fprintf(stderr, "missing -n argument\n");
				return ERR_BADOPT;
			}
			else if (nirqns < 0 || nirqns > 496)
			{
				fprintf(stderr, "-n argument out of range\n");
				return ERR_BADOPT;
			}
			break;
		case 'i':
			addirqs = 1;
			break;
		case 's':
			stdexcnames = 1;
			break;
		default:
			{
				fprintf(stderr, "invalid option %s\n", argv[carg]);
				return ERR_BADOPT;
			}
		}
	}
	
    if (carg >= argc)
    {
        fprintf(stderr, "file not specified\n");
        return 1;
    }
	
	char *in_name = argv[carg];
    FILE *in = fopen(in_name, "r");
    if (in == NULL)
    {
        fprintf(stderr, "%s file not found\n", in_name);
        return ERR_NOFILE;
    }
	
	_Bool irqdefs = 0;
	int maxirqn = -15;
	
	while (fgets(line, MAXLINELEN, in))
	{
		if (!irqdefs && strstr(line, "_IRQn"))
			irqdefs = 1;
		if (irqdefs)
		{
			char irqid[IRQNAMELEN];
			int irqnum;
			
			if (sscanf(line, "%s = %d", irqid, &irqnum) == 2 && irqnum < 496)
			{
				//printf("%3d %s\n", irqnum, irqid);
				irqid[strlen(irqid) - (irqnum >= 0 ? 1 : 4)] = 0;
				if (irqnum > maxirqn)
					maxirqn = irqnum;
				if (irqnum >= -14 && irqnum <= 512)
					strcpy(irqname[irqnum + 16], irqid);
				else
					fprintf(stderr, "error: %s\n", line);
			}
			else if (strchr(line, '}'))
				break;
		}
	}
	fclose(in);

	const char *stripped_in_name = get_stripped_name(in_name);
	
	char ofname[200] = "startup_";
	strcat(ofname, stripped_in_name);
	ofname[strlen(ofname) - 1] = 'c';

    FILE *out = fopen(ofname, "w");
    if (in == NULL)
    {
        fprintf(stderr, "cannot create file %s\n", ofname);
        return ERR_NOFILE;
    }
	
	const char *stripped_out_name = get_stripped_name(ofname);
	
	int mcuirqns = maxirqn + 1;	// no. of NVIC IRQns
	if (nirqns > mcuirqns && !addirqs)	// adjust if no unused vectors will be defined
		nirqns = mcuirqns;
	
	fprintf(out, heading, ofname, stripped_in_name);
	if (stdexcnames)
		fprintf(out, "    Standard short core exception names.\n");
	if (nirqns > -1 && nirqns != mcuirqns)
		fprintf(out, "    %d NVIC IRQ vectors (MCU defines %d).\n", nirqns, mcuirqns);
	if (addirqs)
		fprintf(out, "    Unused vector names defined.\n", nirqns);
	fprintf(out, "*/\n\n");
	
	fprintf(out, defhandler);
	
	//printf("mcu IRQns: %d\n", mcuirqns);
	//printf("last IRQn: %d\n", maxirqn);
	if (nirqns != -1 && maxirqn > nirqns - 1)
		maxirqn = nirqns - 1;
	
    for (int i = 2; i <= maxirqn + 16; i++)
    {
		char proto[50];
		if (irqname[i][0])
		{
			sprintf(proto, "void %sHandler(void)", stdexcnames && i < 16 && stdexcname[i] ? stdexcname[i] : irqname[i]);
			fprintf(out, "%-48s__attribute__ ((weak, alias(\"Default_Handler\")));\n", proto);
		}
		else if (i >= 16 && addirqs)
		{
			sprintf(proto, "void IRQ%d_IRQHandler(void)", i - 16);
			fprintf(out, "%-48s__attribute__ ((weak, alias(\"Default_Handler\")));\n", proto);
		}
    }
	fprintf(out, vtstart);
    for (int i = 2; i < 16; i++)
	{
		if (irqname[i][0])
			fprintf(out, "        CX(%2d) = %sHandler%s", i,
			stdexcnames && stdexcname[i] ? stdexcname[i] : irqname[i],
			i < 15 ? ",\n" : "\n");
	}
	fprintf(out, vtmid);
	int inw = maxirqn > 99 ? 3 : 2;
    for (int i = 16; i <= maxirqn + 16; i++)
    {
		char s[80];
		if (irqname[i][0] || addirqs)
		{
			if (irqname[i][0])
				sprintf(s, "[%*d] = %sHandler", inw, i - 16, irqname[i]);
			else if (addirqs)
				sprintf(s, "[%*d] = IRQ%d_IRQHandler", inw, i - 16, i - 16);
			if (i < maxirqn + 16)
				strcat(s, ",");
			fprintf(out, "        %s\n", s);
		}
    }
	fprintf(out, vtend);
	fclose(out);
}
