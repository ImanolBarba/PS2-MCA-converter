/***************************************************************************
 *   main.c  --  This file is part of PS2-MCA-converter.                   *
 *                                                                         *
 *   Copyright (C) 2023 Imanol-Mikel Barba Sabariego                       *
 *                                                                         *
 *   hdlgi-cli is free software: you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published     *
 *   by the Free Software Foundation, either version 3 of the License,     *
 *   or (at your option) any later version.                                *
 *                                                                         *
 *   hdlgi-cli is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty           *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.               *
 *   See the GNU General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see http://www.gnu.org/licenses/.   *
 *                                                                         *
 *   This project uses ported source code from mymc from Ross Ridge        *
 *   (http://www.csclub.uwaterloo.ca:11068/mymc/index.html)                *
 *                                                                         *
 ***************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ECC_SIZE 16
#define PAGES_PER_BLOCK 16
#define BYTES_PER_PAGE 512
#define BLOCKS_PER_CARD 1024
#define EXPECTED_NON_ECC_MEMCARD_SIZE BLOCKS_PER_CARD * PAGES_PER_BLOCK * BYTES_PER_PAGE
#define EXPECTED_MEMCARD_SIZE EXPECTED_NON_ECC_MEMCARD_SIZE + BLOCKS_PER_CARD * PAGES_PER_BLOCK*ECC_SIZE
#define ECC_BYTES_LENGTH 128

#define VERSION "1.0"

uint8_t parity_table[256];
uint8_t column_parity_masks[256];

static struct option longopts[] = {
    { "help",      no_argument,                  NULL,           'h'       },
    { "version",   no_argument,                  NULL,           'v'       },
    { NULL,        0,                            NULL,           0      }
};

void print_version() {
  printf("ps2_mca_converter %s\n", VERSION);
}

void print_help() {
  printf("ps2_mca_converter [-h --help | -v --version] INPUT OUTPUT\n");
  printf("Small utility to convert PS2 Memory Card Annihilator dumps to regular ps2 format used by PCSX2 and others\n\n");

  printf(" -h --help\n");
  printf("    Print detailed help screen\n\n");

  printf(" -v --version\n");
  printf("    Prints version\n\n");
}

int parity(uint8_t byte) {
  byte = (byte ^ (byte >> 1));
  byte = (byte ^ (byte >> 2));
  byte = (byte ^ (byte >> 4));
  return byte & 1;
}

void make_ecc_tables() {
  for(unsigned int byte = 0; byte < 256; ++byte) {
    parity_table[byte] = parity(byte);
  }
  uint8_t cpmasks[7] = {0x55, 0x33, 0x0F, 0x00, 0xAA, 0xCC, 0xF0};
  for(unsigned int byte = 0; byte < 256; ++byte) {
    uint8_t mask = 0;
    for(unsigned int i = 0; i < 7; ++i) {
      mask |= parity_table[byte & cpmasks[i]] << i;
      column_parity_masks[byte] = mask;
    }
  }
}

void calculate_ecc(uint8_t data[BYTES_PER_PAGE], uint8_t* ecc) {
  for(unsigned int i = 0; i < 4; ++i) {
    uint8_t column_parity = 0x77;
    uint8_t line_parity_0 = 0x7F;
    uint8_t line_parity_1 = 0x7F;
    for(unsigned int pos = 0; pos < ECC_BYTES_LENGTH; ++pos) {
      uint8_t byte = data[ECC_BYTES_LENGTH * i + pos];
      uint8_t column_parity = column_parity ^ column_parity_masks[byte];
      if(parity_table[byte]) {
        line_parity_0 = line_parity_0 ^ ~i;
        line_parity_1 = line_parity_1 ^ i;
      }
    }
    off_t ecc_offset = 3 * i;
    ecc[ecc_offset] = column_parity;
    ecc[ecc_offset+1] = line_parity_0 & 0x7F;
    ecc[ecc_offset+2] = line_parity_1;
  }
}

const char* get_filename_from_fd(int fd) {
  char* filename = calloc(4096, 1);
  #ifdef __linux__
    char fdpath[1024];
    snprintf(fdpath, 1024, "/proc/self/fd/%d", fd);
    if(readlink(fdpath, filename, 4096) == -1) {
      fprintf(stderr, "Unable to get filename: %s\n", strerror(errno));
      return NULL;
    }
  #else
    if(fcntl(fd, F_GETPATH, filename) == -1) {
      fprintf(stderr, "Unable to get filename: %s\n", strerror(errno));
      return NULL;
    }
  #endif

  return filename;
}

void close_fd(int fd) {
  if(close(fd) == -1) {
    const char* filename = get_filename_from_fd(fd);
    fprintf(stderr, "Unable to close file %s: %s\n", filename, strerror(errno));
    free((void*)filename);
  }
}

int read_file(int fd, uint8_t* buf, size_t len) {
  size_t total_read = 0;
  ssize_t num_read = 0;
  while(total_read != len) {
    num_read = read(fd, buf + total_read, len - total_read);
    if(num_read == -1) {
      if(errno != EINTR) {
        fprintf(stderr, "Error reading input memcard: %s\n", strerror(errno));
        return 1;
      }
    } else {
      total_read += num_read;
    }
  }
  return 0;
}

int write_file(int fd, uint8_t* buf, size_t len) {
  size_t total_written = 0;
  ssize_t num_written = 0;
  while(total_written != len) {
    num_written = write(fd, buf + total_written, len - total_written);
    if(num_written == -1) {
      if(errno != EINTR) {
        return 1;
      }
    } else {
      total_written += num_written;
    }
  }
  return 0;
}

size_t get_file_size(int fd) {
  off_t file_size = lseek(fd, 0, SEEK_END);
  if(file_size == -1) {
    fprintf(stderr, "Unable to seek to the end of input: %s\n", strerror(errno));
    return 1;
  }

  if(lseek(fd, 0, SEEK_SET) == -1) {
    fprintf(stderr, "Unable to seek to the start of input: %s\n", strerror(errno));
    return 1;
  }

  return (size_t)file_size;
}

int convert_memcard(int in_fd, int out_fd) {
  size_t file_size = get_file_size(in_fd);
  if(file_size != EXPECTED_NON_ECC_MEMCARD_SIZE) {
    fprintf(stderr, "Input Memory Card has an unexpected size: %zu\n", file_size);
    return 1;
  }

  uint8_t data[BYTES_PER_PAGE];
  for(unsigned int block = 0; block < BLOCKS_PER_CARD; ++block) {
    fprintf(stderr, "\x1b[A");
    fprintf(stderr, "Block %d of %d\n", block+1, BLOCKS_PER_CARD);
    for(unsigned int page = 0; page < PAGES_PER_BLOCK; ++page) {
      if(read_file(in_fd, data, BYTES_PER_PAGE)) {
        fprintf(stderr, "Error reading input memcard: %s\n", strerror(errno));
        return 1;
      }

      if(write_file(out_fd, data, BYTES_PER_PAGE)) {
        fprintf(stderr, "Error writing output memcard: %s\n", strerror(errno));
        return 1;
      }

      uint8_t ecc[ECC_SIZE];
      memset(ecc, 0x00, ECC_SIZE);
      calculate_ecc(data, ecc);
      if(write_file(out_fd, ecc, ECC_SIZE)) {
        fprintf(stderr, "Error writing output memcard: %s\n", strerror(errno));
        return 1;
      }
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  int c;
  int long_index = 0;

  if(argc == 1) {
    fprintf(stderr, "No arguments provided\n");
    print_help();
    return 1;
  }

  while ((c = getopt_long(argc, argv, "hv", longopts, &long_index)) != -1) {
    if(c == 'v') {
      print_version();
      return 0;
    }
    else if(c == 'h') {
      print_help();
      return 0;
    }
  }

  if(optind != argc-2) {
    fprintf(stderr, "Invalid number of positional arguments\n");
    print_help();
    return 1;
  }

  fprintf(stderr, "Generating parity tables...\n");
  make_ecc_tables();
  fprintf(stderr, "Finished parity tables generation\n");


  int in_fd = open(argv[1], O_RDONLY, 0644);
  if(in_fd < 0) {
    fprintf(stderr, "Unable to open source memory card %s: %s\n", argv[1], strerror(errno));
    return 1;
  }
  fprintf(stderr, "Opened input memcard successfully (%s)\n", argv[1]);

  int out_fd = open(argv[2], O_WRONLY | O_CREAT, 0644);
  if(out_fd < 0) {
    fprintf(stderr, "Unable to open destination memory card %s: %s\n", argv[2], strerror(errno));
    close_fd(in_fd);
    return 1;
  }
  fprintf(stderr, "Opened output memcard successfully (%s)\n", argv[2]);

  fprintf(stderr, "Begin conversion\n");
  int ret = convert_memcard(in_fd, out_fd);
  close_fd(in_fd);
  close_fd(out_fd);

  if(ret) {
    fprintf(stderr, "Unable to convert memcard\n");
  } else {
    fprintf(stderr, "Successfully converted memcard\n");
  }

  return ret;
}
