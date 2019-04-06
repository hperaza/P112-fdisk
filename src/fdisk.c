/**************************************************************************

  GIDE FDISK utility for the P112.
  Copyright (C) 2004-2006, Hector Peraza.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    
***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNITS_SECTORS    0
#define UNITS_UZITRACKS  1
#define UNITS_CYLINDERS  2

#define MAX_ENTRIES      8

struct EntryType {
    unsigned char code;
    char *name;
};

struct EntryType entry_types[] = {
    0x00, "Empty",
    0x52, "CP/M",
    0xB2, "CP/M 3.0",
    0xD1, "UZI",
    0xD2, "UZI swap"
};

#define NUM_ET  sizeof(entry_types)/sizeof(entry_types[0])

#ifdef UZI
#define DEFAULT_PTYPE  entry_types[2].code
#else
#define DEFAULT_PTYPE  entry_types[0].code
#endif

#define METHOD_STD  0
#define METHOD_BP   1

void print_menu();
void ide_geometry();
void read_ptable();
void write_ptable();
void change_units();
void show_partitions();
void show_geometry();
void show_method();
void add_partition();
void delete_partition();
void list_types();
char *type_str(int num);
void set_type();
void toggle_bootable();
void toggle_method();
void verify_table();

unsigned char hdbuf[1024];         /* new-style boot code is 2 sectors long */

char *p112sign = "P112GIDE";

int  hdcyls, hdheads, hdsecs;      /* disk geometry, as stored in ptable */
int  idecyls, ideheads, idesecs;   /* disk geometry, as reported by the disk */
int  units, valid, idok;
int  method, ptoffs, goffs, sgnoffs;
char *filename;

/* The partition table */

struct { 
    unsigned int  start;
    unsigned int  size;
    unsigned char type;
    unsigned char bflag;
} ptable[MAX_ENTRIES];

/* For the IDE identify command */

struct IDRecord {
    short config;
    short NumCyls;
    short NumCyls2;
    short NumHeads;
    short BytesPerTrk;
    short BytesPerSec;
    short SecsPerTrack;
    short d1, d2, d3;
    char  SerNo[20];
    short CtrlType;
    short BfrSize;
    short ECCBytes;
    char  CtrlRev[8];
    char  CtrlModl[40];
    short SecsPerInt;
    short DblWordFlag;
    short WrProtect;
    short res1;
    short PIOtiming;
    short DMAtiming;
    short res2;
    short CurCyls;
    short CurHeads;
    short CurSPT;
};

struct IDRecord idbuf;

/* Functions defined in gideio.asz */

extern int hdident(struct IDRecord *buf);
extern int hdread(int cyl, int head, int sector, unsigned char *buf);
extern int hdwrite(int cyl, int head, int sector, unsigned char *buf);


/* from linker, location of boot loader assembly code */
extern unsigned char *_Bldboot,  *_Lldboot,  *_Hldboot; /* old-style loader */
extern unsigned char *_Bboot,    *_Lboot,    *_Hboot;

extern unsigned char *_Bldnboot, *_Lldnboot, *_Hldnboot; /* new-style loader */
extern unsigned char *_Bnboot,   *_Lnboot,   *_Hnboot;

int main(int argc, char *argv[])
{
    FILE *f;
    char cmd[100];

    printf("P112 FDISK version 1.1 (GIDE)\n");

    filename = NULL;
    if (argc > 1) {
        filename = argv[1];
        f = fopen(filename, "rb");
        if (!f) {
            fprintf(stderr, "Could not open file %s.\n", filename);
            return 1;
        }
        if (fread(hdbuf, 1, 1024, f) <= 0) {
            fprintf(stderr, "Error reading file %s.\n", filename);
            fclose(f);
            return 1;
        }
        fclose(f);
    } else {
        if (hdread(0, 0, 0, hdbuf) || hdread(0, 0, 1, hdbuf+512)) {
            fprintf(stderr, "Could not read partition table: hard disk failure.\n");
            return 1;
        }
    }

    ptoffs = 3;   /* offset to partition table pointer */
    goffs = 5;    /* offset to disk geometry pointer */
    sgnoffs = 7;  /* offset to signature */
    method = METHOD_STD;
    units = UNITS_UZITRACKS;

    ide_geometry();
    read_ptable();
    show_geometry();
    show_method();

    printf("\n");

    for (;;) {
        printf("Command (h for help): ");
        fgets(cmd, 100, stdin);
        
        if (cmd[0] == '\n') continue;
        
        switch (tolower(cmd[0])) {
        case 'b':
            toggle_bootable();
            break;

        case 'd':
            delete_partition();
            break;

        case 'l':
            list_types();
            break;

        case 'm':
            toggle_method();
            break;

        case 'n':
            add_partition();
            break;

        case 'p':
            show_partitions();
            break;

        case 'q':
            return 0;

        case 't':
            set_type();
            break;

        case 'u':
            change_units();
            break;

        case 'v':
            verify_table();
            break;

        case 'w':
            write_ptable();
            return 0;

        case 'x':
            break;

        default:
            print_menu();
            break;
        }
    }
}

void print_menu()
{
    printf("\n");
    printf("Command action\n");
    printf("   b    toggle a bootable flag\n");
    printf("   d    delete a partition\n");
    printf("   h    print this menu\n");
    printf("   l    list known partition types\n");
    printf("   m    toggle boot code method\n");
    printf("   n    add a new partition\n");
    printf("   p    print the partition table\n");
    printf("   q    quit without saving\n");
    printf("   t    change a partition's system id\n");
    printf("   u    change display/entry units\n");
    printf("   v    verify the partition table\n");
    printf("   w    write table to disk and exit\n");
/*
    printf("   x    extra functionality (experts only)\n");
*/
    printf("\n");
}

void ide_geometry()
{
    if (hdident(&idbuf)) {
         fprintf(stderr, "Could not read drive ID\n");
         idok = 0;
    } else {
         idecyls = idbuf.NumCyls;
         ideheads = idbuf.NumHeads;
         idesecs = idbuf.SecsPerTrack;
         idok = 1;
         if (!valid) {
             hdcyls = idecyls;
             hdheads = ideheads;
             hdsecs = idesecs;
         } else {
             if ((hdcyls != idecyls) ||
                 (hdheads != ideheads) ||
                 (hdsecs != idesecs)) {
                 fprintf(stderr, "The disk geometry stored in the "
                                 "partition table does not match\n"
                                 "the one reported by the disk.\n");
                 /* show_geometry(); */
             }
         }
    }
}

void read_ptable()
{
    int i, bootsz, cyls, heads, sectors, *p;
    unsigned char cks, *b;

    valid = 1;
    bootsz = 512;

    for (i = 0; i < MAX_ENTRIES; ++i) {
        ptable[i].start = 0;
        ptable[i].size  = 0;
        ptable[i].type  = 0;
        ptable[i].bflag = 0;
    }

    /* do some validation checks first */

    if (hdbuf[0] == 0x76) {
        /* looks like a new-style boot sector */
        if (hdbuf[1] == 0x21) {
            /* check for a 'P112GIDE' signature */
            ptoffs = 17;
            goffs = 19;
            sgnoffs = 8;
            method = METHOD_BP;
            bootsz = 1024;
            for (i = 0; i < 8; ++i) {
                if (hdbuf[i+sgnoffs] != p112sign[i]) {
                    valid = 0;
                    break;
                }
            }
            if (!valid) {
                printf("\n");
                printf("This disk seems to have a B/P BIOS boot record.\n");
                printf("It will be overwritten by this program, proceed at your own risk.\n");
                return;
            }
        }
    } else if (hdbuf[0] == 0xC3) {
        /* check for a 'P112GIDE' signature */
        ptoffs = 3;
        goffs = 5;
        sgnoffs = 7;
        method = METHOD_STD;
        bootsz = 512;
        for (i = 0; i < 8; ++i) {
            if (hdbuf[i+sgnoffs] != p112sign[i]) {
                valid = 0;
                break;
            }
        }
        /* shouldn't we check for the version number as well? */
        if (valid) {
            /* looks OK so far, let's do some safety checks */
            p = (int *) &hdbuf[ptoffs];
            if ((*p < 7) || (*p > bootsz)) valid = 0;
            p = (int *) &hdbuf[goffs];
            if ((*p < 7) || (*p > bootsz)) valid = 0;
        }
    } else {
        valid = 0;
    }

    /* we should still check for a valid disk geometry definition */
    
    if (method == METHOD_STD) {
        for (i = 0, cks = 0; i < 512; ++i) cks += hdbuf[i];
        if (cks != 0) valid = 0;
    }

    if (!valid) {
        printf("This disk does not have a valid P112 partition or boot record.\n");
        return;
    }

    p = (int *) &hdbuf[goffs];
    p = (int *) &hdbuf[*p];
    b = (unsigned char *) p;
    
    cyls = *p;
    heads = *(b+2);
    sectors = *(b+3);

    /* we should still check for a valid disk geometry definition */

    p = (int *) &hdbuf[ptoffs];
    p = (int *) &hdbuf[*p];

    for (i = 0; i < MAX_ENTRIES; ++i) {
        ptable[i].start = *p++;
        ptable[i].size = *p++;
        b = (unsigned char *) p++;
        ptable[i].type = *b++;
        ptable[i].bflag = *b;
    }

    hdcyls = cyls;
    hdheads = heads;
    hdsecs = sectors;
}

void write_ptable()
{
    FILE *f;
    int  i, cks, *p, boot_size, max_size;
    unsigned char *boot_code, *b;
    
    if (method == METHOD_BP) {
        boot_code = (unsigned char *) &_Bldnboot;
        boot_size = (int) &_Hldnboot - (int) &_Lldnboot +
                    (int) &_Hnboot - (int) &_Lnboot;   /* ugly, ugly... */
    } else {
        boot_code = (unsigned char *) &_Bldboot;
        boot_size = (int) &_Hldboot - (int) &_Lldboot +
                    (int) &_Hboot - (int) &_Lboot;
    }

    if (method == METHOD_STD) {
        /* This is not suppossed to happen, but we'll check anyway... */
        max_size = 512;
        if ((boot_size <= 0) || (boot_size > max_size)) {
            printf("Internal error: boot loader code size is %d\n", boot_size);
            if (valid) {
                printf("Using original boot loader code.\n");
            } else {
                printf("Unable to write new partition table.\n\n");
                /* perhaps we should allow the user to specify a filename
                   containing a valid boot loader */
                return;
            }
        } else {
            /* copy the new code */
            for (i = 0; i < max_size; ++i) hdbuf[i] = 0;
            for (i = 0; i < boot_size; ++i) hdbuf[i] = *boot_code++;
            /* shouldn't we do some pointer validations here as well? */
        }
    } else {
        max_size = 1024;
        if ((boot_size <= 0) || (boot_size > max_size)) {
            printf("Internal error: boot loader code size is %d\n", boot_size);
            if (valid) {
                printf("Using original boot loader code.\n");
            } else {
                printf("Unable to write new partition table.\n\n");
                /* perhaps we should allow the user to specify a filename
                   containing a valid boot loader */
                return;
            }
        } else {
            /* copy the new code */
            for (i = 0; i < max_size; ++i) hdbuf[i] = 0;
            for (i = 0; i < boot_size; ++i) hdbuf[i] = *boot_code++;
            /* shouldn't we do some pointer validations here as well? */
        }
    }

    p = (int *) &hdbuf[ptoffs];
    p = (int *) &hdbuf[*p];

    for (i = 0; i < MAX_ENTRIES; ++i) {
        if (ptable[i].size == 0) {
            *p++ = 0;
            *p++ = 0;
            *p++ = 0;
        } else {
            *p++ = ptable[i].start;
            *p++ = ptable[i].size;
            b = (unsigned char *) p++;
            *b++ = ptable[i].type;
            *b = ptable[i].bflag;
        }
    }

    /* copy the disk geometry values as well */

    p = (int *) &hdbuf[goffs];
    p = (int *) &hdbuf[*p];
    b = (unsigned char *) p;
    
    *p = idecyls; /*hdcyls;*/
    *(b+2) = (unsigned char) ideheads; /*hdheads;*/
    *(b+3) = (unsigned char) idesecs; /*hdsecs;*/

    /* compute the checksum */

    if (method == METHOD_STD) {
        for (i = 0, cks = 0; i < 511; ++i) cks += hdbuf[i];
        hdbuf[511] = -cks;
    }

    if (filename) {
        f = fopen(filename, "wb");
        if (!f) {
            fprintf(stderr, "Could not create file %s.\n\n", filename);
            return;
        }
        if (fwrite(hdbuf, 1, max_size, f) != max_size) {
            fprintf(stderr, "Error writing file %s.\n", filename);
            fclose(f);
            return;
        }
        fclose(f);
    } else {
        if (hdwrite(0, 0, 0, hdbuf)) {
            fprintf(stderr, "Could not write partition table: hard disk failure.\n");
            return;
        }
        if (method == METHOD_BP) {
            if (hdwrite(0, 0, 1, hdbuf+512)) {
                fprintf(stderr, "Could not write partition table: hard disk failure.\n");
                return;
            }
        }
    }
    
    printf("Done.\n\n");
}

void change_units()
{
    char *ustr;

    if (++units > UNITS_CYLINDERS) units = UNITS_SECTORS;
    
    switch (units) {
    case UNITS_SECTORS:
        ustr = "sectors";
        break;

    default:
        units = UNITS_UZITRACKS;
    case UNITS_UZITRACKS:
        ustr = "UZI180 tracks (16 sectors)";
        break;

    case UNITS_CYLINDERS:
        ustr = "cylinders";
        break;
    }
    
    printf("Changing display/entry units to %s\n\n", ustr);
}

void show_partitions()
{
    int i;

    printf("\n");
    printf("Partition  Start    End   Size     Bytes  Bootable  Type\n");
    printf("---------  -----  -----  -----  --------  --------  ------\n");

    for (i = 0; i < MAX_ENTRIES; ++i) {
        if (ptable[i].size > 0) {
            printf("%5d      %5d  %5d  %5d  %8lu      %s     %s\n",
                   i + 1,
                   ptable[i].start,
                   ptable[i].start + ptable[i].size - 1,
                   ptable[i].size,
                   (unsigned long) ptable[i].size * 8192L,
                   ptable[i].bflag ? "Y" : " ",
                   type_str(ptable[i].type));
        }
    }
    printf("\n");
}

void show_geometry()
{
    unsigned long csecs, cbytes;

    printf("\n");
    printf("Hard disk geometry:\n");

    if (idok) {
        csecs = (unsigned long) idecyls *
                (unsigned long) ideheads *
                (unsigned long) idesecs;
        cbytes = csecs * (unsigned long) 512;
    
        printf("\n");
        printf("  As reported by the drive: %d cylinders, %d heads %d sectors\n",
               idecyls, ideheads, idesecs);
        printf("  Capacity: %lu sectors (%lu bytes)\n", csecs, cbytes);
    }

    if (valid) {
        csecs = (unsigned long) hdcyls *
                (unsigned long) hdheads *
                (unsigned long) hdsecs;
        cbytes = csecs * (unsigned long) 512;
    
        printf("  As stored in the partition table: %d cylinders, %d heads %d sectors\n",
                hdcyls, hdheads, hdsecs);
        printf("  Capacity: %lu sectors (%lu bytes)\n", csecs, cbytes);
    }
    
    printf("\n");
    printf("Display/entry units are in UZI180 tracks (16 sectors or 8192 bytes)\n\n");
}

void show_method()
{
    if (method == METHOD_BP)
        printf("Using new-style boot sector code\n");
    else
        printf("Using standard boot sector code\n");
}

void add_partition()
{
    int  i, n, val, first_free, max_cyl;
    unsigned long hdsecs;
    char c, str[20];

    hdsecs = (unsigned long) idecyls *
             (unsigned long) ideheads *
             (unsigned long) idesecs;
    max_cyl = (hdsecs / 16L) - 1;

    first_free = 1;
    for (i = 0; i < MAX_ENTRIES; ++i) {
        if (ptable[i].size == 0) break;
        first_free = ptable[i].start + ptable[i].size;
    }
    if (i == MAX_ENTRIES) {
        printf("The table is full. You must delete some partition first.\n\n");
        return;
    }

    printf("Partition number (%d-%d): ", i+1, MAX_ENTRIES);
    fgets(str, 20, stdin);
    n = atoi(str);
    if ((n < 1) || (n > MAX_ENTRIES)) {
        printf("Value out of range.\n\n");
        return;
    }
    --n;
    if (ptable[n].size != 0) {
        printf("Partition %d is already defined. Delete it before re-adding it.\n\n", n + 1);
        return;
    }

    printf("First cylinder (%d-%d, default %d): ",
                           first_free, max_cyl - 1, first_free);
    fgets(str, 20, stdin);
    if (str[0] == '\n') {
        printf("Using default value %d\n", first_free);
        val = first_free;
    } else {
        val = atoi(str);
    }
    
    if ((val < 0) || (val > max_cyl)) {
        printf("Value out of range\n");
        return;
    }
    ptable[n].start = val;

    printf("Last cylinder or +size or +sizeM or +sizeK (%d-%d, default %d): ",
                           val + 1, max_cyl, max_cyl);
    fgets(str, 20, stdin);
    if (str[0] == '\n') {
        printf("Using default value %d\n", max_cyl);
        val = max_cyl - ptable[n].start;
    } else {
        if (str[0] == '+') {
            val = atoi(str+1);
            c = str[strlen(str)-2];  /* get last character, skipping newline */
            if ((c == 'M') || (c == 'm')) {
                val = val * 128;     /* convert to cyls (1 cyl = 8k) */
            } else if ((c == 'K') || (c == 'k')) {
                i = val % 8;
                val = val / 8;
                if (i != 0) ++val;
            }
        } else {
            val = atoi(str);
            val -= ptable[n].start;
            if (val <= 0) {
                printf("Last cylinder must be larger than first cylinder.\n");
                return;
            }
        }
    }
    ptable[n].size = val;

    ptable[n].bflag = 0;
    ptable[n].type = DEFAULT_PTYPE;
    printf("\n");
}

void delete_partition()
{
    int  n;
    char str[20];

    printf("Partition number (1-%d): ", MAX_ENTRIES);
    fgets(str, 20, stdin);
    n = atoi(str);
    if ((n < 1) || (n > MAX_ENTRIES)) {
        printf("Value out of range.\n\n");
        return;
    }
    --n;

    if (ptable[n].size == 0) {
        printf("Partition %d is already deleted.\n\n", n+1);
        return;
    }

    ptable[n].size = 0;
    printf("\n");
}

void list_types()
{
    int i;
    
    printf("\n");
    for (i = 0; i < NUM_ET; ++i) {
        printf(" %02x  %s\n", entry_types[i].code, entry_types[i].name);
    }
    printf("\n");
}

char *type_str(int num) {
    int i;
    
    for (i = 0; i < NUM_ET; ++i) {
        if (entry_types[i].code == num) return entry_types[i].name;
    }
    return "Unknown";
}

void set_type()
{
    int  n, type;
    char str[20];

    printf("Partition number (1-%d): ", MAX_ENTRIES);
    fgets(str, 20, stdin);
    n = atoi(str);
    if ((n < 1) || (n > MAX_ENTRIES)) {
        printf("Value out of range.\n\n");
        return;
    }
    --n;

    if (ptable[n].size == 0) {
        printf("Partition %d does not exist yet.\n\n", n+1);
        return;
    }

    for (;;) {
      printf("Hex code (type L to list codes): ");
      fgets(str, 20, stdin);
      if ((str[0] == 'L') || (str[0] == 'l')) {
        list_types();
      } else if (sscanf(str, "%x", &type) == 1) {
        ptable[n].type = type;
        break;
      }
    }

    printf("Changed system type of partition %d to %02x (%s)\n",
           n+1, type, type_str(type));
    printf("\n");
}

void toggle_bootable()
{
    int  n;
    char str[20];

    printf("Partition number (1-%d): ", MAX_ENTRIES);
    fgets(str, 20, stdin);
    n = atoi(str);
    if ((n < 1) || (n > MAX_ENTRIES)) {
        printf("Value out of range.\n\n");
        return;
    }
    --n;

    if (ptable[n].size == 0) {
        printf("Partition %d does not exist yet.\n\n", n+1);
        return;
    }

    ptable[n].bflag = !ptable[n].bflag;
    printf("\n");
}

void toggle_method()
{
    if (method == METHOD_BP) {
        ptoffs = 3;
        goffs = 5;
        sgnoffs = 7;
        method = METHOD_STD;
    } else {
        ptoffs = 17;
        goffs = 19;
        sgnoffs = 8;
        method = METHOD_BP;
    }
    show_method();
}

void verify_table()
{
    unsigned long allocsecs, hdsecs, ovlpsecs;
    int  *p, i, j, cyls, heads, sectors;
    char *b;
    
    p = (int *) &hdbuf[goffs];
    p = (int *) &hdbuf[*p];
    b = (char *) p;
    
    cyls = *p;
    heads = *(b+2);
    sectors = *(b+3);
    
    hdsecs = (unsigned long) cyls * (unsigned long) heads * (unsigned long) sectors;
    
    /* check for overlapping partitions */

    ovlpsecs = 0;
    for (i = 0; i < MAX_ENTRIES; ++i) {
        for (j = i + 1; j < MAX_ENTRIES; ++j) {
            if (ptable[j].size == 0) continue;
            if ((ptable[j].start < ptable[i].start + ptable[i].size) &&
                (ptable[j].start + ptable[j].size > ptable[i].start)) {
                printf("Partition %d overlaps partition %d\n", j+1, i+1);
                /*ovlpsecs += */
            }
        }
    }

    /* calculate the number of unallocated sectors */

    allocsecs = 0;
    for (i = 0; i < MAX_ENTRIES; ++i) {
        allocsecs += ptable[i].size * 16;
    }
    allocsecs -= ovlpsecs;

    if (hdsecs > allocsecs) printf("%lu unallocated sectors.\n", hdsecs - allocsecs);

    /* this shouldn't happen, since add_partition() takes care of
       not over-allocating sectors, but anyway we could be dealing here
       with a wrong or corrupt partition table */
    if (hdsecs < allocsecs) printf("%lu overallocated sectors.\n", allocsecs - hdsecs);

    if (ovlpsecs > 0) printf("%d overlapped sectors\n", ovlpsecs);

    printf("\n");
}
