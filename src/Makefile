all: fdisk fdisk.com hdboot hdnboot

fdisk.obj: fdisk.c
	zxc -o -v -c $<

gideio.obj: gideio.asz
	zxas -n $<

# As the fdisk program grows larger, the bss link address has to be increased!

fdisk: fdisk.obj gideio.obj hdboot.obj hdnboot.obj
	@echo "-Z -W3 -Dfdiskuzi.sym \\" > linkcmd.uzi
	@echo "-Ptext=0,data,ldboot=8000h/,boot=0C000h/,ldnboot=8000h/,nboot=0C000h/,bss=5000h/ \\" >> linkcmd.uzi
	@echo "-C100H -o$@ \\" >> linkcmd.uzi
	@echo "crt.obj fdisk.obj gideio.obj hdboot.obj hdnboot.obj \\" >> linkcmd.uzi
	@echo "uzilibc.lib" >> linkcmd.uzi
	zxcc link -"<" +linkcmd.uzi

fdisk.com: fdisk.obj gideio.obj hdboot.obj hdnboot.obj
	@echo "-Z -W3 -Dfdisk.sym \\" > linkcmd.cpm
	@echo "-Ptext=0,data,ldboot=8000h/,boot=0C000h/,ldnboot=8000h/,nboot=0C000h/,bss=5000h/ \\" >> linkcmd.cpm
	@echo "-C100H -ofdisk.com \\" >> linkcmd.cpm
	@echo "crtcpm.obj fdisk.obj gideio.obj hdboot.obj hdnboot.obj \\" >> linkcmd.cpm
	@echo "cpmlibc.lib" >> linkcmd.cpm
	zxcc link -"<" +linkcmd.cpm

hdboot.obj: hdboot.asz
	zxas -n $<

hdboot: hdboot.obj
	zxlink -Z -W3 -Pldboot=8000h/0,boot=0C000h/ -c -o$@ $@.obj

hdnboot.obj: hdnboot.asz
	zxas -n $<

hdnboot: hdnboot.obj
	zxlink -Z -W3 -Pldnboot=8000h/0,nboot=0C000h/ -c -o$@ $@.obj

clean:
	rm -f fdisk fdisk.com fdisk.obj gideio.obj
	rm -f hdboot hdboot.obj
	rm -f hdnboot hdnboot.obj
	rm -f core *~ *.\$$\$$\$$ *.sym

rmbak:
	rm -f *~ core *.\$$\$$\$$
