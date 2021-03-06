#include <fstream>
#include <ctime>
#include <stdio.h>
#include <cstdlib>

#include "preprocess/preprocessor.h"
#include "coder/encoder.h"
#include "coder/decoder.h"
#include "predictor.h"

int Help() {
  printf("cmix version 13\n");
  printf("With preprocessing:\n");
  printf("    compress:           cmix -c [dictionary] [input] [output]\n");
  printf("    only preprocessing: cmix -s [dictionary] [input] [output]\n");
  printf("    decompress:         cmix -d [dictionary] [input] [output]\n");
  printf("Without preprocessing:\n");
  printf("    compress:   cmix -c [input] [output]\n");
  printf("    decompress: cmix -d [input] [output]\n");
  return -1;
}

void WriteHeader(unsigned long long length, std::ofstream* os) {
  for (int i = 4; i >= 0; --i) {
    char c = length >> (8*i);
    os->put(c);
  }
}

void WriteHeader(unsigned long long length, FILE* out) {
  for (int i = 4; i >= 0; --i) {
    char c = length >> (8*i);
    putc(c, out);
  }
}

void ReadHeader(std::ifstream* is, unsigned long long* length) {
  *length = 0;
  for (int i = 0; i < 5; ++i) {
    *length <<= 8;
    *length += (unsigned char)(is->get());
  }
}

void Compress(unsigned long long input_bytes, std::ifstream* is,
    std::ofstream* os, unsigned long long* output_bytes, Predictor* p) {
  Encoder e(os, p);
  unsigned long long percent = 1 + (input_bytes / 100);
  for (unsigned long long pos = 0; pos < input_bytes; ++pos) {
    char c = is->get();
    for (int j = 7; j >= 0; --j) {
      e.Encode((c>>j)&1);
    }
    if (pos % percent == 0) {
      printf("\rprogress: %lld%%", pos / percent);
      fflush(stdout);
    }
  }
  e.Flush();
  *output_bytes = os->tellp();
}

void Decompress(unsigned long long output_length, std::ifstream* is,
                std::ofstream* os, Predictor* p) {
  Decoder d(is, p);
  unsigned long long percent = 1 + (output_length / 100);
  for(unsigned long long pos = 0; pos < output_length; ++pos) {
    int byte = 1;
    while (byte < 256) {
      byte += byte + d.Decode();
    }
    os->put(byte);
    if (pos % percent == 0) {
      printf("\rprogress: %lld%%", pos / percent);
      fflush(stdout);
    }
  }
}

bool Store(const std::string& input_path, const std::string& temp_path,
    const std::string& output_path, FILE* dictionary,
    unsigned long long* input_bytes, unsigned long long* output_bytes) {
  FILE* data_in = fopen(input_path.c_str(), "rb");
  if (!data_in) return false;
  FILE* data_out = fopen(output_path.c_str(), "wb");
  if (!data_out) return false;
  fseek(data_in, 0L, SEEK_END);
  *input_bytes = ftell(data_in);
  fseek(data_in, 0L, SEEK_SET);
  WriteHeader(0, data_out);
  preprocessor::encode(data_in, data_out, *input_bytes, temp_path, dictionary);
  fseek(data_out, 0L, SEEK_END);
  *output_bytes = ftell(data_out);
  fclose(data_in);
  fclose(data_out);
  return true;
}

bool RunCompression(bool enable_preprocess, const std::string& input_path,
    const std::string& temp_path, const std::string& output_path,
    FILE* dictionary, unsigned long long* input_bytes,
    unsigned long long* output_bytes) {
  Predictor p;
  std::string path = temp_path;
  if (enable_preprocess) {
    FILE* data_in = fopen(input_path.c_str(), "rb");
    if (!data_in) return false;
    FILE* temp_out = fopen(temp_path.c_str(), "wb");
    if (!temp_out) return false;

    fseek(data_in, 0L, SEEK_END);
    *input_bytes = ftell(data_in);
    fseek(data_in, 0L, SEEK_SET);

    preprocessor::encode(data_in, temp_out, *input_bytes, temp_path,
        dictionary);
    fclose(data_in);
    fclose(temp_out);
    preprocessor::pretrain(&p, dictionary);
  } else {
    path = input_path;
  }

  std::ifstream temp_in(path, std::ios::in | std::ios::binary);
  if (!temp_in.is_open()) return false;
  std::ofstream data_out(output_path, std::ios::out | std::ios::binary);
  if (!data_out.is_open()) return false;

  temp_in.seekg(0, std::ios::end);
  unsigned long long temp_bytes = temp_in.tellg();
  if (!enable_preprocess) *input_bytes = temp_bytes;
  temp_in.seekg(0, std::ios::beg);

  WriteHeader(temp_bytes, &data_out);
  Compress(temp_bytes, &temp_in, &data_out, output_bytes, &p);
  temp_in.close();
  data_out.close();
  if (enable_preprocess) remove(temp_path.c_str());
  return true;
}

bool RunDecompression(bool enable_preprocess, const std::string& input_path,
    const std::string& temp_path, const std::string& output_path,
    FILE* dictionary, unsigned long long* input_bytes,
    unsigned long long* output_bytes) {
  std::ifstream data_in(input_path, std::ios::in | std::ios::binary);
  if (!data_in.is_open()) return false;

  data_in.seekg(0, std::ios::end);
  *input_bytes = data_in.tellg();
  data_in.seekg(0, std::ios::beg);
  ReadHeader(&data_in, output_bytes);

  if (*output_bytes == 0) {  // undo store
    if (!enable_preprocess) return false;
    data_in.close();
    FILE* in = fopen(input_path.c_str(), "rb");
    if (!in) return false;
    FILE* data_out = fopen(output_path.c_str(), "wb");
    if (!data_out) return false;
    fseek(in, 5L, SEEK_SET);
    preprocessor::decode(in, data_out, temp_path, dictionary);
    fseek(data_out, 0L, SEEK_END);
    *output_bytes = ftell(data_out);
    fclose(in);
    fclose(data_out);
    return true;
  }
  Predictor p;
  if (enable_preprocess) preprocessor::pretrain(&p, dictionary);

  std::ofstream temp_out(temp_path, std::ios::out | std::ios::binary);
  if (!temp_out.is_open()) return false;

  Decompress(*output_bytes, &data_in, &temp_out, &p);
  data_in.close();
  temp_out.close();

  if (enable_preprocess) {
    FILE* temp_in = fopen(temp_path.c_str(), "rb");
    if (!temp_in) return false;
    FILE* data_out = fopen(output_path.c_str(), "wb");
    if (!data_out) return false;

    preprocessor::decode(temp_in, data_out, temp_path, dictionary);
    fseek(data_out, 0L, SEEK_END);
    *output_bytes = ftell(data_out);
    fclose(temp_in);
    fclose(data_out);
    remove(temp_path.c_str());
  }
  return true;
}

int main(int argc, char* argv[]) {
  if (argc < 4 || argc > 5 || argv[1][0] != '-' ||
      (argv[1][1] != 'c' && argv[1][1] != 'd' && argv[1][1] != 's')) {
    return Help();
  }

  clock_t start = clock();

  bool enable_preprocess = false;
  std::string input_path = argv[2];
  std::string output_path = argv[3];
  FILE* dictionary = NULL;
  if (argc == 5) {
    enable_preprocess = true;
    dictionary = fopen(argv[2], "rb");
    if (!dictionary) return Help();
    input_path = argv[3];
    output_path = argv[4];
  }

  std::string temp_path = output_path;
  if (enable_preprocess) temp_path += ".cmix.temp";

  unsigned long long input_bytes = 0, output_bytes = 0;

  if (argv[1][1] == 's') {
    if (!enable_preprocess) return Help();
    if (!Store(input_path, temp_path, output_path, dictionary, &input_bytes,
        &output_bytes)) {
      return Help();
    }
  } else if (argv[1][1] == 'c') {
    if (!RunCompression(enable_preprocess, input_path, temp_path, output_path,
        dictionary, &input_bytes, &output_bytes)) {
      return Help();
    }
  } else {
    if (!RunDecompression(enable_preprocess, input_path, temp_path, output_path,
        dictionary, &input_bytes, &output_bytes)) {
      return Help();
    }
  }

  printf("\r%lld bytes -> %lld bytes in %1.2f s.\n",
      input_bytes, output_bytes,
      ((double)clock() - start) / CLOCKS_PER_SEC);

  if (argv[1][1] == 'c') {
    double cross_entropy = output_bytes;
    cross_entropy /= input_bytes;
    cross_entropy *= 8;
    printf("cross entropy: %.3f\n", cross_entropy);
  }

  return 0;
}
