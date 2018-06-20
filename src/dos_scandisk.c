/*
* SCANDISK FOR FAT-12
*
* Copyright Theo Turner 2016
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

/*
* REFERENCED
*
* This function is a modified version of follow_dir from dos_scandisk.c. It takes an
* additional argument, the array read_clusters, which represents which parts of the
* volume can be read. Array index corresponds to volume contents. This function runs
* through the contents of all directory entries, marking them 1 in the array.
*
*/
void referenced(uint16_t cluster, int indent,
		uint8_t *image_buf, struct bpb33* bpb, int read_clusters[]) {
    struct direntry *dirent;
    int d, i;
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    while (1) {
	for (d = 0; d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; 
	     d += sizeof(struct direntry)) {
	    char name[9];
	    char extension[4];
	    uint16_t file_cluster;
	    name[8] = ' ';
	    extension[3] = ' ';
	    memcpy(name, &(dirent->deName[0]), 8);
	    memcpy(extension, dirent->deExtension, 3);
	    if (name[0] == SLOT_EMPTY)
		return;

	    /* skip over deleted entries */
	    if (((uint8_t)name[0]) == SLOT_DELETED)
		continue;

	    /* names are space padded - remove the spaces */
	    for (i = 8; i > 0; i--) {
		if (name[i] == ' ') 
		    name[i] = '\0';
		else 
		    break;
	    }

	    /* remove the spaces from extensions */
	    for (i = 3; i > 0; i--) {
		if (extension[i] == ' ') 
		    extension[i] = '\0';
		else 
		    break;
	    }

	    /* don't print "." or ".." directories */
	    if (strcmp(name, ".")==0) {
		dirent++;
		continue;
	    }
	    if (strcmp(name, "..")==0) {
		dirent++;
		continue;
	    }

	    file_cluster = getushort(dirent->deStartCluster);
	    if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
		read_clusters[file_cluster] = 1;
	    } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
		read_clusters[file_cluster] = 1;
		referenced(file_cluster, indent+2, image_buf, bpb, read_clusters);
	    } else {
		while (!is_end_of_file(file_cluster)) {
			read_clusters[file_cluster] = 1;
			file_cluster = get_fat_entry(file_cluster, image_buf, bpb);
		}
	    }
	    dirent++;
	}
	if (cluster == 0) {
	    // root dir is special
	    dirent++;
	} else {
	    cluster = get_fat_entry(cluster, image_buf, bpb);
	    dirent = (struct direntry*)cluster_to_addr(cluster, 
						       image_buf, bpb);
	}
    }
}

/*
* LENGTH INCONSISTENCY
*
* This function is a modified version of follow_dir from dos_scandisk.c. It runs
* through the clusters that consist files; if it reaches a number greater than the
* declared length of the file, it writes a file terminator and frees subsequent
* clusters. It continues until it reaches the end of file delimiter, at which point
* it declares the file size in the directory entry and what the actual file size
* was (now having been corrected).
*
*/
void length_inconsistency(uint16_t cluster, int indent,
		uint8_t *image_buf, struct bpb33* bpb) {
    struct direntry *dirent;
    int d, i;
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    while (1) {
	for (d = 0; d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; 
	     d += sizeof(struct direntry)) {
	    char name[9];
	    char extension[4];
	    uint32_t size, actual_size, size_of_clusters;
	    uint16_t file_cluster, previous_cluster;
	    int check, j = 0;
	    name[8] = ' ';
	    extension[3] = ' ';
	    memcpy(name, &(dirent->deName[0]), 8);
	    memcpy(extension, dirent->deExtension, 3);
	    if (name[0] == SLOT_EMPTY)
		return;

	    /* skip over deleted entries */
	    if (((uint8_t)name[0]) == SLOT_DELETED)
		continue;

	    /* names are space padded - remove the spaces */
	    for (i = 8; i > 0; i--) {
		if (name[i] == ' ') 
		    name[i] = '\0';
		else 
		    break;
	    }

	    /* remove the spaces from extensions */
	    for (i = 3; i > 0; i--) {
		if (extension[i] == ' ') 
		    extension[i] = '\0';
		else 
		    break;
	    }

	    /* don't print "." or ".." directories */
	    if (strcmp(name, ".")==0) {
		dirent++;
		continue;
	    }
	    if (strcmp(name, "..")==0) {
		dirent++;
		continue;
	    }

	    file_cluster = getushort(dirent->deStartCluster);
	    if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
		continue;
	    } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
		length_inconsistency(file_cluster, indent+2, image_buf, bpb);
	    } else {
		size = getulong(dirent->deFileSize);
		size_of_clusters = (size + (511)) / 512;
		check = 0;
		while (!is_end_of_file(file_cluster)) {
			previous_cluster = file_cluster;
			file_cluster = get_fat_entry(file_cluster, image_buf, bpb);
			if (j >= size_of_clusters - 1) {
				if (check == 0) {
					set_fat_entry(previous_cluster, FAT12_MASK&CLUST_EOFS, image_buf, bpb);
					check = 1;
				}
				else {
					set_fat_entry(previous_cluster, 0, image_buf, bpb);
				}
			}
			j++;
		}
		actual_size = j * 512;
		if (size_of_clusters != j) {
			printf("%s.%s %u %u\n", name, extension, size, actual_size);
		}
	    }
	    dirent++;
	}
	if (cluster == 0) {
	    // root dir is special
	    dirent++;
	} else {
	    cluster = get_fat_entry(cluster, image_buf, bpb);
	    dirent = (struct direntry*)cluster_to_addr(cluster, 
						       image_buf, bpb);
	}
    }
}

/*
* WRITE DIRECTORY ENTRY
*
* This is a copy of write_dirent from dos_cp.c. It writes a directory entry for use
* with create_dirent.
*
*/
void write_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size) {
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) {
	if (p2[i] == '/' || p2[i] == '\\') {
	    uppername = p2+i+1;
	}
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) {
	uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) {
	fprintf(stderr, "No filename extension given - defaulting to .___\n");
    } else {
	*p = '\0';
	p++;
	len = strlen(p);
	if (len > 3) len = 3;
	memcpy(dirent->deExtension, p, len);
    }
    if (strlen(uppername)>8) {
	uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* a real filesystem would set the time and date here, but it's
       not necessary for this coursework */
}

/*
* CREATE DIRECTORY ENTRY
*
* This is a copy of create_dirent from dos_cp.c. It uses write_dirent to create
* a directory entry in available space.
*
*/
void create_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size,
		   uint8_t *image_buf, struct bpb33* bpb) {
    while(1) {
	if (dirent->deName[0] == SLOT_EMPTY) {
	    /* we found an empty slot at the end of the directory */
	    write_dirent(dirent, filename, start_cluster, size);
	    dirent++;

	    /* make sure the next dirent is set to be empty, just in
	       case it wasn't before */
	    memset((uint8_t*)dirent, 0, sizeof(struct direntry));
	    dirent->deName[0] = SLOT_EMPTY;
	    return;
	}
	if (dirent->deName[0] == SLOT_DELETED) {
	    /* we found a deleted entry - we can just overwrite it */
	    write_dirent(dirent, filename, start_cluster, size);
	    return;
	}
	dirent++;
    }
}


/*
* UNREFERENCED
*
* This function checks for unreferenced items by listing which entries in read_clusters
* are still 0 after referenced has run, excluding free space.
*
*/
void unreferenced(uint8_t *image_buf, struct bpb33* bpb, int number_of_clusters, int read_clusters[]) {
	int i, check = 0;
	for (i = 2; i < number_of_clusters; i++) {
	    if ((read_clusters[i] == 0) && (get_fat_entry(i, image_buf, bpb) != 0)) {
		if (check == 0) {
			printf("Unreferenced:");
			check = 1;
		}
		printf(" %i", i);
	    }
	}
	if (check == 1) {
		printf("\n");
	}
}

/*
* LOST FILES
*
* This function marks lost files and creates a directory entry for each. It reads the
* drive, outputting the cluster number when it first sees an unreferenced entry. It
* continues until it hits an end of file marker, at which point it prints the number
* of clusters read. Finally, a directory entry of that size is created in the same
* location, overwriting the unreferenced clusters.
*
*/
void lost_files(uint8_t *image_buf, struct bpb33* bpb, int number_of_clusters, int read_clusters[], char file_name[]) {
	int i;
	int file_number = 1;
	struct direntry *dirent = (struct direntry*) cluster_to_addr(0, image_buf, bpb);
	uint16_t start;
	uint32_t size = 0;
	for (i = 2; i < number_of_clusters; i++) {
	    if ((start == 0) && (read_clusters[i] == 0) && (get_fat_entry(i, image_buf, bpb) != 0)) {
		printf("Lost file: %i", i);
		start = i;
	    }
	    if ((start != 0) && (is_end_of_file(get_fat_entry(i, image_buf, bpb)))) {
		size = i - start + 1;
		printf(" %i\n", size);
		snprintf(file_name, sizeof(char) * 12, "found%i.dat", file_number);
		create_dirent(dirent, file_name, start, size * 512, image_buf, bpb);
		file_number++;
		start = 0;
	    }
	}
}

/*
* USAGE
*
* This function is called if input cannot be parsed. It prints the correct usage format.
*
*/
void usage() {
	fprintf(stderr, "Usage: dos_scandisk <imagename>\n");
	exit(1);
}

/*
* MAIN
*
* This function initialises required variables, then calls referenced, unreferenced,
* lost_files and length_inconsistency in order.
*
*/
int main(int argc, char **argv) {
	uint8_t *image_buf;
	int fd;
	struct bpb33* bpb;
	if (argc < 2 || argc > 2) {
		usage();
	}
	image_buf = mmap_file(argv[1], &fd);
	bpb = check_bootsector(image_buf);
	int number_of_clusters = (bpb->bpbSectors) / (bpb->bpbSecPerClust);
	int read_clusters[number_of_clusters];
	char file_name[12];
	referenced(0, 0, image_buf, bpb, read_clusters);
	unreferenced(image_buf, bpb, number_of_clusters, read_clusters);
	lost_files(image_buf, bpb, number_of_clusters, read_clusters, file_name);
	length_inconsistency(0, 0, image_buf, bpb);
	close(fd);
	exit(0);
}
