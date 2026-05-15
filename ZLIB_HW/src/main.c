#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "debug.h"
#include "global.h"
#include "our_zlib.h"
#include "crc.h"

int main(int argc, char** argv) {
	if (argc == 1) { PRINT_ERROR_MISSING_I_FLAG(); return EXIT_FAILURE; }

	/* Pass 1: find -i filename and handle -h */
	uint8_t name = 0;
	char* filename = NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '-' && name == 0) {
			fprintf(stderr, "Error: Unknown option %s\n", argv[i]); return EXIT_FAILURE;
		}
		else if (argv[i][0] != '-' && name == 1) { filename = argv[i]; break; }
		else if (argv[i][0] != '-') break;
		size_t arg_len = strlen(argv[i]) - 1;
		char arg[arg_len + 1];
		strncpy(arg, argv[i] + 1, arg_len);
		arg[arg_len] = '\0';
		for (int j = 0; j < arg_len; j++) {
			if (arg[j] == 'h') { PRINT_USAGE(argv[0]); return EXIT_SUCCESS; }
			else if (arg[j] == 'i' && name == 0) name = 1;
		}
	}
	if (name == 0) { PRINT_ERROR_MISSING_I_FLAG(); return EXIT_FAILURE; }
	else if (filename == NULL) { PRINT_ERROR_MISSING_I_FLAG(); return EXIT_FAILURE; }
	FILE* fp = fopen(filename, "rb");
	if (fp == NULL) { PRINT_ERROR_OPEN_FILE(filename); return EXIT_FAILURE; }
	fclose(fp); fp = NULL;

	/* Pass 2: process mode flags (-m, -c, -d) and -o */
	int mode = -1;
	uint8_t o = 0;
	char* output_filename = NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '-' && strcmp(argv[i], filename) != 0 && !o) {
			fprintf(stderr, "Error: Unknown option %s\n", argv[i]); return EXIT_FAILURE;
		}
		else if (argv[i][0] != '-' && strcmp(argv[i], filename) == 0) continue;
		else if (argv[i][0] != '-' && o == 1) { output_filename = argv[i]; o++; continue; }
		else if (argv[i][0] != '-') break;
		size_t arg_len = strlen(argv[i]) - 1;
		char arg[arg_len + 1];
		strncpy(arg, argv[i] + 1, arg_len);
		arg[arg_len] = '\0';
		for (int j = 0; j < arg_len; j++) {
			if (arg[j] == 'm') {
				if (mode >= 0) { PRINT_ERROR_REQUIRE_ONE_OF_MCD(); return EXIT_FAILURE; }
				mode = M_INFO;
			}
			else if (arg[j] == 'c') {
				if (mode >= 0) { PRINT_ERROR_REQUIRE_ONE_OF_MCD(); return EXIT_FAILURE; }
				mode = M_DEFLATE;
			}
			else if (arg[j] == 'd') {
				if (mode >= 0) { PRINT_ERROR_REQUIRE_ONE_OF_MCD(); return EXIT_FAILURE; }
				mode = M_INFLATE;
			}
			else if (arg[j] == 'o' && !o) o++;
			else if (arg[j] != 'i' && arg[j] != 'h') {
				char matta[3];
				matta[0] = '-'; matta[1] = arg[j]; matta[2] = '\0';
				fprintf(stderr, "Error: Unknown option %s\n", matta);
				return EXIT_FAILURE;
			}
		}
	}

	if (mode < 0) { PRINT_ERROR_REQUIRE_ONE_OF_MCD(); return EXIT_FAILURE; }
	if ((mode == M_DEFLATE || mode == M_INFLATE) && output_filename == NULL) {
		PRINT_ERROR_MISSING_O_FLAG(); return EXIT_FAILURE;
	}

	FILE* file = fopen(filename, "rb");
	if (file == NULL) { PRINT_ERROR_OPEN_FILE(filename); return EXIT_FAILURE; }

	gz_header_t info = {0};
	if (mode != M_DEFLATE) {
		if (parse_member(file, &info) != 0) {
			PRINT_ERROR_BAD_HEADER();
			fclose(file);
			return EXIT_FAILURE;
		}
	}

	switch (mode) {
		case M_INFO: {
			rewind(file);
			PRINT_MEMBER_SUMMARY_HEADER(filename);
			int member_idx = 0;
			while (1) {
				long member_start = ftell(file);
				if (parse_member(file, &info) != 0) break;
				char label[256];
				if (info.name && info.name[0])
					snprintf(label, sizeof(label), "%s", info.name);
				else
					snprintf(label, sizeof(label), "%d", member_idx);
				long fs = ftell(file); fseek(file, member_start, SEEK_SET);
				skip_gz_header_to_compressed_data(file, &info);
				size_t cl = (size_t)(fs - 8 - ftell(file)); char* cb = malloc(cl);
				if (!cb) { member_idx++; fseek(file, fs, SEEK_SET); continue; }
				fread(cb, 1, cl, file); char* ob = inflate(cb, cl); free(cb);
				int crc_valid = 0;
				if (ob) {
					unsigned int calcd = get_crc((unsigned char*)ob, info.full_size);
					crc_valid = (calcd == info.crc);
				}
				free(ob);
				PRINT_MEMBER_LINE(label, info.cm, info.mtime, info.os,
					(unsigned)info.extra_len, info.comment, info.full_size, crc_valid);
				member_idx++;
				fseek(file, fs, SEEK_SET);
			}
			break;
		}
		case M_DEFLATE: {
			if (fseek(file, 0, SEEK_END) != 0) {
				fclose(file);
				return EXIT_FAILURE;
			}
			long file_size = ftell(file);
			if (file_size < 0) {
				fclose(file);
				return EXIT_FAILURE;
			}
			rewind(file);
			char* input = malloc((size_t)file_size);
			if (!input || fread(input, 1, (size_t)file_size, file) != (size_t)file_size) {
				free(input);
				fclose(file);
				return EXIT_FAILURE;
			}
			size_t out_len = 0;
			char* out_buf = deflate(filename, input, (size_t)file_size, &out_len);
			free(input);
			fclose(file);
			if (!out_buf) return EXIT_FAILURE;
			FILE* out = fopen(output_filename, "wb");
			if (!out) {
				PRINT_ERROR_OPEN_FILE(output_filename);
				free(out_buf);
				return EXIT_FAILURE;
			}
			if (out_len > 0 && fwrite(out_buf, 1, out_len, out) != out_len) {
				fclose(out);
				free(out_buf);
				return EXIT_FAILURE;
			}
			fclose(out);
			free(out_buf);
			break;
		}
		case M_INFLATE: {
			if (fseek(file, 0, SEEK_END) != 0) {
				fclose(file);
				return EXIT_FAILURE;
			}
			long file_size = ftell(file);
			if (file_size < 8) {
				fclose(file);
				return EXIT_FAILURE;
			}
			rewind(file);
			if (skip_gz_header_to_compressed_data(file, &info) != 0) {
				fclose(file);
				return EXIT_FAILURE;
			}
			long comp_start = ftell(file);
			if (comp_start < 0 || comp_start + 8 > file_size) {
				fclose(file);
				return EXIT_FAILURE;
			}
			size_t comp_len = (size_t)(file_size - 8 - comp_start);
			char* comp_buf = malloc(comp_len);
			if (!comp_buf || fread(comp_buf, 1, comp_len, file) != comp_len) {
				free(comp_buf);
				fclose(file);
				return EXIT_FAILURE;
			}
			fclose(file);
			char* out_buf = inflate(comp_buf, comp_len);
			free(comp_buf);
			if (!out_buf) return EXIT_FAILURE;
			unsigned int calcd = get_crc((unsigned char*)out_buf, info.full_size);
			if (calcd != info.crc) PRINT_ERROR_BAD_HEADER();
			FILE* out = fopen(output_filename, "wb");
			if (!out) {
				PRINT_ERROR_OPEN_FILE(output_filename);
				free(out_buf);
				return EXIT_FAILURE;
			}
			if (info.full_size > 0 && fwrite(out_buf, 1, info.full_size, out) != info.full_size) {
				fclose(out);
				free(out_buf);
				return EXIT_FAILURE;
			}
			fclose(out);
			free(out_buf);
			break;
		}
		default:
			PRINT_ERROR_REQUIRE_ONE_OF_MCD();
			break;
	}

	if (mode != M_DEFLATE && mode != M_INFLATE)
		fclose(file);
	return EXIT_SUCCESS;
}
