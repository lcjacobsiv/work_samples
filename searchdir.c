/*
 * File: searchdir.c
 * Author: Lawrence Jacobs
 *
 * This program recursively searches a given directory
 * for all files whose names/inode numbers/or last modified
 * dates match user input.
 * The program can be run with just a search string and 
 * directory name, in which case it will print all files in 
 * the specified directory whose names match the input string.
 *
 * The program can also be run as an inode search, in which
 * case the program assembles a map of all files within the 
 * specified directory and repeatedly asks user to provide
 * inode numbers, printing filenames with matching numbers 
 * should any exist.
 *
 * Finally the program can be run as a date search, in which
 * case the program assembles a map of all files within the 
 * specified directory and repeatedly asks user to provide
 * dates (MM/DD), printing filenames with matching last
 * modified dates, should any exist.
 * ----------------------
 */

#include "cmap.h"
#include "cvector.h"
#include <dirent.h>
#include <error.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

#define NFILES_ESTIMATE 20
#define MAX_INODE_LEN   21  // digits in max unsigned long is 20, plus \0
#define SENTINEL "q"
#define DATE_MAX 6

typedef void (*GatherFn)(const char *fullpath, struct stat ss, void *aux);
typedef void (*CleanupElemFn)(void *addr);

/*Cleanup function for CVector and CMap with char* pointers as keys*/
void clean_fn(void *addr){
    free(*(char**)addr);
}

/*Cleanup function for CMap with CVectors as keys*/
void clean_dates(void *addr){
    CVector *vec = *(CVector**) addr;
    cvec_dispose(vec);
}

/*Comparison function for ino_t types */
int cmp(const void *a, const void *b){
    return *(ino_t*)a - *(ino_t*)b;
}

/*Comparison for path length*/
int cmp_length(const void *a, const void *b){
    char *str1 = *(char**)a;
    char *str2 = *(char**)b;
    if(strlen(str1) < strlen(str2)) return -1;
    if(strlen(str1) == strlen(str2)) return 0;
    return 1;
}

/*This function handles adding file paths to a CVector*/
void gather_vector(const char *fullpath, struct stat ss, void *aux){
    char str[strlen(fullpath)+1];
    strncpy(str, fullpath, strlen(fullpath));
    str[strlen(fullpath)] = '\0';
    char *str_heaped = strdup(str);
    CVector *matches = (CVector *) aux;
    cvec_append(matches, &str_heaped);
}

/*This function handles adding inodes (key) and file paths (value) to a CMap*/
void gather_map(const char *fullpath, struct stat ss, void *aux){
    char str[strlen(fullpath)+1];
    strncpy(str, fullpath, strlen(fullpath));
    str[strlen(fullpath)] = '\0';
    char *str_heaped = strdup(str);
    
    char inodestr[MAX_INODE_LEN];
    unsigned long inode = ss.st_ino;    /*inode number is unique id per entry in filesystem*/
    sprintf(inodestr, "%lu", inode);

    CMap *map = (CMap *) aux;
    cmap_put(map, inodestr, &str_heaped);   /*add inode/path pair to map*/
}

/*This function handles adding dates (key) and file paths (value) to a CMap*/
void gather_dates(const char *fullpath, struct stat ss, void *aux){
    char str[strlen(fullpath)+1];
    strncpy(str, fullpath, strlen(fullpath));
    str[strlen(fullpath)] = '\0';
    char *str_heaped = strdup(str);
    
    /*Write the last modified date of the file to a string*/
    struct tm *timeobj = localtime(&(ss.st_mtime));
    char datebuf[DATE_MAX];
    strftime(datebuf, DATE_MAX, "%m/%d", timeobj);
    
    /*Add date/filepath pair to CMap*/
    CMap *map = (CMap*) aux;
    if(cmap_get(map, datebuf) == NULL){/*If there's no vector entry for this date, create a new one*/
        CVector *new_vec = cvec_create(sizeof(char*), NFILES_ESTIMATE, (CleanupElemFn) clean_fn);
        cvec_append(new_vec, &str_heaped);
        cmap_put(map, datebuf, &new_vec);
    } else {  /*else, find extant entry and appent filename*/
        CVector *matched_vec = *(CVector**) cmap_get(map, datebuf);
        cvec_append(matched_vec, &str_heaped);
    }
}

/*Recursive function to gather files within a directory (as well as its subdirectories)
 *Adds files to CVector or CMap, depending on the input data structure (matches)*/
static void gather_files(void *matches, const char *searchstr, const char *dirname, 
                                CVector *visited, GatherFn gatherer)
{
    DIR *dp = opendir(dirname); 
    struct dirent *entry;
    while (dp != NULL && (entry = readdir(dp)) != NULL) { /* iterate over entries in dir */
        if (cvec_search(visited, &entry->d_ino, (CompareFn) cmp, 0, false) != -1) continue;
        cvec_append(visited, &entry->d_ino);
        if (entry->d_name[0] == '.') continue; /* skip hidden files */

        char fullpath[PATH_MAX];
        sprintf(fullpath, "%s/%s", dirname, entry->d_name); /* construct full path */
        
        struct stat ss;
        int result = stat(fullpath, &ss);
        if (result == 0 && S_ISDIR(ss.st_mode))  /* if subdirectory, recur */
            gather_files(matches, searchstr, fullpath, visited, gatherer);
        /*else ensure searchstr matches filename and add file to matches*/
        else if((strlen(searchstr) ==  0) || (strstr(entry->d_name, searchstr)) != NULL)
            gatherer(fullpath, ss, matches);   /*else add to vector*/
        }
    closedir(dp);
}

/*Function for searching for files by date. Constructs a map of dates to vectors
 *of path+filenames. Asks user for modification dates and prints file paths with
 * matching dates*/
static void datesearch(const char *dirname){
    /*Set up map and populate date/filename information*/
    CMap *map = cmap_create(sizeof(char*), NFILES_ESTIMATE, (CleanupElemFn) clean_dates);
    CVector *visited = cvec_create(sizeof(ino_t), NFILES_ESTIMATE, NULL);
    gather_files(map, "", dirname, visited, gather_dates);
    
    /*Request dates to search from user until sentinel is typed*/
    char input[DATE_MAX];
    printf("Enter date MM/DD (or q to quit): ");
    int num = scanf("%s", input);   /*write date user provides to input*/
    while(num != EOF && (strcmp(input, SENTINEL) != 0)){
        if(cmap_get(map, input) != NULL){ /*Retrieve vector for specified date*/
            CVector *vecvalue = *(CVector**) cmap_get(map, input);
            for (char *cur = cvec_first(vecvalue); cur != NULL; cur = cvec_next(vecvalue, cur))
                printf("%s\n", *(char**) cur); /*Print all paths */
            printf("Enter date MM/DD (or q to quit): ");
            num = scanf("%s", input);
        } else {    /*else, if no vector exists for this date, request new input*/
            printf("Enter date MM/DD (or q to quit): ");
            num = scanf("%s", input);
        }
    }

    cmap_dispose(map);
    cvec_dispose(visited);
}

/*Function for searching for files by inode. Constructs a map of inode numbers
 *to path+filenames. Asks user for inode numbers and prints file paths with
 * matching inodes*/
static void inodesearch(const char *dirname){
    /*Gather files*/
    CMap *map = cmap_create(sizeof(char*), NFILES_ESTIMATE, (CleanupElemFn) clean_fn);
    CVector *visited = cvec_create(sizeof(ino_t), NFILES_ESTIMATE, NULL);
    gather_files(map, "", dirname, visited, gather_map);

    /*Take user input*/
    char input[MAX_INODE_LEN];
    printf("Enter inode (or q to quit): ");
    int num = scanf("%s", input);
    while(num != EOF && (strcmp(input, SENTINEL) != 0)){
        if(cmap_get(map, input) != NULL){
            printf("%s\n", *(char**)cmap_get(map, input));
            printf("Enter inode (or q to quit): ");
            num = scanf("%s", input);
        }else{
            printf("Enter inode (or q to quit): ");
            num = scanf("%s", input);
        }
    }

    cmap_dispose(map);
    cvec_dispose(visited);
}

/*Function for searching for files by name. Constructs a vector of paths
 *to files and prints matched files in order of pathlength*/
static void namesearch(const char *searchstr, const char *dirname)
{
    /*Gather files*/
    CVector *matches = cvec_create(sizeof(char*), NFILES_ESTIMATE, (CleanupElemFn) clean_fn);
    CVector *visited = cvec_create(sizeof(ino_t), NFILES_ESTIMATE, NULL);
    gather_files(matches, searchstr, dirname, visited, gather_vector);
    
    /*Sort and print files*/
    cvec_sort(matches, (CompareFn) cmp_length);
    for (char *cur = cvec_first(matches); cur != NULL; cur = cvec_next(matches, cur))
        printf("%s\n", *(char**) cur);

    cvec_dispose(matches);
    cvec_dispose(visited);
}

int main(int argc, char *argv[])
{
    if (argc < 2) error(1,0, "Usage: searchdir [-d or -i or searchstr] [(optional) directory].");

    char *dirname = argc < 3 ? "." : argv[2];
    if (access(dirname, R_OK) == -1) error(1,0, "cannot access path \"%s\"", dirname);

    if (0 == strcmp("-d", argv[1])) {
        datesearch(dirname);            /*search by date*/
    } else if (0 == strcmp("-i", argv[1])) {
        inodesearch(dirname);           /*search by inode*/
    } else {
        namesearch(argv[1], dirname);   /*search by name*/
    }
    return 0;
}

