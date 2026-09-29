#include <fstream>
#include <ctime>
#include <stdio.h>
#include <cstdlib>
#include <vector>
#include <string.h>

#include "preprocess/preprocessor.h"
#include "coder/encoder.h"
#include "coder/decoder.h"
#include "predictor.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

namespace {
  const int kMinVocabFileSize = 10000;
}

char* dictionary_path = NULL;

int Help(int exit_code = -1) {
  printf("cmix version 21 (Context Mixing Archiver & Compressor)\n");
  printf("Usage:\n");
  printf("  Compress:\n");
  printf("    cmix -c [dictionary] <input> <output>  (Compress with optional dictionary)\n");
  printf("    cmix -c <input> <output>               (Compress without dictionary)\n");
  printf("    cmix -n <input> <output>               (No preprocessing)\n");
  printf("    cmix -s [dictionary] <input> <output>  (Only preprocessing)\n");
  printf("    cmix -t [dictionary] <input> <output>  (Force text-mode)\n");
  printf("  Decompress:\n");
  printf("    cmix -d [dictionary] <input> <output>  (Decompress with optional dictionary)\n");
  printf("    cmix -d <input> <output>               (Decompress without dictionary)\n");
  printf("  Test integrity:\n");
  printf("    cmix -t <archive>                      (Verify archive integrity)\n");
  printf("  Streams / Pipes (tar compatible):\n");
  printf("    cmix [-c|-d] < <input> > <output>\n");
  printf("  Information:\n");
  printf("    cmix -h | --help                       (Display this help message)\n");
  printf("    cmix -v | -V | --version               (Display version)\n");
  return exit_code;
}

void WriteHeader(unsigned long long length, const std::vector<bool>& vocab,
    bool dictionary_used, std::ofstream* os) {
  for (int i = 4; i >= 0; --i) {
    char c = length >> (8*i);
    if (i == 4) {
      c &= 0x7F;
      if (dictionary_used) c |= 0x80;
    }
    os->put(c);
  }
  if (length < kMinVocabFileSize) return;
  for (int i = 0; i < 32; ++i) {
    unsigned char c = 0;
    for (int j = 0; j < 8; ++j) {
      if (vocab[i * 8 + j]) c += 1<<j;
    }
    os->put(c);
  }
}

void WriteStorageHeader(FILE* out, bool dictionary_used) {
  for (int i = 4; i >= 0; --i) {
    char c = 0;
    if (i == 4 && dictionary_used) c = 0x80;
    putc(c, out);
  }
}

void ReadHeader(std::ifstream* is, unsigned long long* length,
    bool* dictionary_used, std::vector<bool>* vocab) {
  *length = 0;
  for (int i = 0; i <= 4; ++i) {
    *length <<= 8;
    unsigned char c = is->get();
    if (i == 0) {
      if (c&0x80) *dictionary_used = true;
      else *dictionary_used = false;
      c &= 0x7F;
    }
    *length += c;
  }
  if (*length == 0) return;
  if (*length < kMinVocabFileSize) {
    std::fill(vocab->begin(), vocab->end(), true);
    return;
  }
  for (int i = 0; i < 32; ++i) {
    unsigned char c = is->get();
    for (int j = 0; j < 8; ++j) {
      if (c & (1<<j)) (*vocab)[i * 8 + j] = true;
    }
  }
}

void ExtractVocab(unsigned long long input_bytes, std::ifstream* is,
    std::vector<bool>* vocab) {
  constexpr size_t BUF_SIZE = 65536;
  char buf[BUF_SIZE];
  while (input_bytes > 0) {
    size_t to_read = std::min<unsigned long long>(input_bytes, BUF_SIZE);
    is->read(buf, to_read);
    std::streamsize n = is->gcount();
    if (n <= 0) break;
    for (std::streamsize i = 0; i < n; ++i) {
      (*vocab)[static_cast<unsigned char>(buf[i])] = true;
    }
    input_bytes -= n;
  }
}

void ClearOutput() {
  fprintf(stderr, "\r                     \r");
  fflush(stderr);
}

void Compress(unsigned long long input_bytes, std::ifstream* is,
    std::ofstream* os, unsigned long long* output_bytes, Predictor* p) {
  Encoder e(os, p);
  unsigned long long percent = 1 + (input_bytes / 10000);
  ClearOutput();
  constexpr size_t BUF_SIZE = 65536;
  char buf[BUF_SIZE];
  unsigned long long pos = 0;
  while (pos < input_bytes) {
    size_t to_read = std::min<unsigned long long>(input_bytes - pos, BUF_SIZE);
    is->read(buf, to_read);
    std::streamsize n = is->gcount();
    if (n <= 0) break;
    for (std::streamsize i = 0; i < n; ++i) {
      char c = buf[i];
      for (int j = 7; j >= 0; --j) {
        e.Encode((c>>j)&1);
      }
      if (pos % percent == 0) {
        double frac = 100.0 * pos / input_bytes;
        fprintf(stderr, "\rprogress: %.2f%%", frac);
        fflush(stderr);
      }
      pos++;
    }
  }
  e.Flush();
  *output_bytes = os->tellp();
}

void Decompress(unsigned long long output_length, std::ifstream* is,
                std::ofstream* os, Predictor* p) {
  Decoder d(is, p);
  unsigned long long percent = 1 + (output_length / 10000);
  ClearOutput();
  constexpr size_t BUF_SIZE = 65536;
  char buf[BUF_SIZE];
  size_t buf_pos = 0;
  for(unsigned long long pos = 0; pos < output_length; ++pos) {
    int byte = 1;
    while (byte < 256) {
      byte += byte + d.Decode();
    }
    buf[buf_pos++] = static_cast<char>(byte);
    if (buf_pos == BUF_SIZE) {
      os->write(buf, BUF_SIZE);
      buf_pos = 0;
    }
    if (pos % percent == 0) {
      double frac = 100.0 * pos / output_length;
      fprintf(stderr, "\rprogress: %.2f%%", frac);
      fflush(stderr);
    }
  }
  if (buf_pos > 0) {
    os->write(buf, buf_pos);
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
  WriteStorageHeader(data_out, dictionary != NULL);
  fprintf(stderr, "\rpreprocessing...");
  fflush(stderr);
  preprocessor::Encode(data_in, data_out, false, *input_bytes, temp_path,
      dictionary);
  fseek(data_out, 0L, SEEK_END);
  *output_bytes = ftell(data_out);
  fclose(data_in);
  fclose(data_out);
  return true;
}

bool RunCompression(bool enable_preprocess, bool text_mode,
    const std::string& input_path, const std::string& temp_path,
    const std::string& output_path, FILE* dictionary,
    unsigned long long* input_bytes, unsigned long long* output_bytes) {
  FILE* data_in = fopen(input_path.c_str(), "rb");
  if (!data_in) return false;
  FILE* temp_out = fopen(temp_path.c_str(), "wb");
  if (!temp_out) return false;

  fseek(data_in, 0L, SEEK_END);
  *input_bytes = ftell(data_in);
  fseek(data_in, 0L, SEEK_SET);

  if (enable_preprocess) {
    fprintf(stderr, "\rpreprocessing...");
    fflush(stderr);
    preprocessor::Encode(data_in, temp_out, text_mode, *input_bytes, temp_path,
        dictionary);
  } else {
    preprocessor::NoPreprocess(data_in, temp_out, *input_bytes);
  }
  fclose(data_in);
  fclose(temp_out);

  std::ifstream temp_in(temp_path, std::ios::in | std::ios::binary);
  if (!temp_in.is_open()) return false;

  std::ofstream data_out(output_path, std::ios::out | std::ios::binary);
  if (!data_out.is_open()) return false;

  temp_in.seekg(0, std::ios::end);
  unsigned long long temp_bytes = temp_in.tellg();
  temp_in.seekg(0, std::ios::beg);

  std::vector<bool> vocab(256, false);
  if (temp_bytes < kMinVocabFileSize) {
    std::fill(vocab.begin(), vocab.end(), true);
  } else {
    ExtractVocab(temp_bytes, &temp_in, &vocab);
    temp_in.seekg(0, std::ios::beg);
  }

  WriteHeader(temp_bytes, vocab, dictionary != NULL, &data_out);
  Predictor p(vocab);
  if (enable_preprocess) preprocessor::Pretrain(&p, dictionary);
  Compress(temp_bytes, &temp_in, &data_out, output_bytes, &p);
  temp_in.close();
  data_out.close();
  remove(temp_path.c_str());
  return true;
}

bool RunDecompression(const std::string& input_path,
    const std::string& temp_path, const std::string& output_path,
    FILE* dictionary, unsigned long long* input_bytes,
    unsigned long long* output_bytes) {
  std::ifstream data_in(input_path, std::ios::in | std::ios::binary);
  if (!data_in.is_open()) return false;

  data_in.seekg(0, std::ios::end);
  *input_bytes = data_in.tellg();
  data_in.seekg(0, std::ios::beg);
  std::vector<bool> vocab(256, false);
  bool dictionary_used;
  ReadHeader(&data_in, output_bytes, &dictionary_used, &vocab);
  if (!dictionary_used && dictionary != NULL) return false;
  if (dictionary_used && dictionary == NULL) return false;

  if (*output_bytes == 0) {  // undo store
    data_in.close();
    FILE* in = fopen(input_path.c_str(), "rb");
    if (!in) return false;
    FILE* data_out = fopen(output_path.c_str(), "wb");
    if (!data_out) return false;
    fseek(in, 5L, SEEK_SET);
    fprintf(stderr, "\rdecoding...");
    fflush(stderr);
    preprocessor::Decode(in, data_out, dictionary);
    fseek(data_out, 0L, SEEK_END);
    *output_bytes = ftell(data_out);
    fclose(in);
    fclose(data_out);
    return true;
  }
  Predictor p(vocab);
  if (dictionary_used) preprocessor::Pretrain(&p, dictionary);

  std::ofstream temp_out(temp_path, std::ios::out | std::ios::binary);
  if (!temp_out.is_open()) return false;

  Decompress(*output_bytes, &data_in, &temp_out, &p);
  data_in.close();
  temp_out.close();

  FILE* temp_in = fopen(temp_path.c_str(), "rb");
  if (!temp_in) return false;
  FILE* data_out = fopen(output_path.c_str(), "wb");
  if (!data_out) return false;

  preprocessor::Decode(temp_in, data_out, dictionary);
  fseek(data_out, 0L, SEEK_END);
  *output_bytes = ftell(data_out);
  fclose(temp_in);
  fclose(data_out);
  remove(temp_path.c_str());
  return true;
}

int main(int argc, char* argv[]) {
  char mode = 0; // 'c', 'd', 'n', 's', 't'
  bool to_stdout = false;
  bool force = false;
  bool keep = false;
  std::string dict_file;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return Help(0);
    } else if (arg == "-v" || arg == "-V" || arg == "--version") {
      printf("cmix version 21\n");
      return 0;
    } else if (arg == "-d" || arg == "--decompress" || arg == "-x" || arg == "--extract") {
      mode = 'd';
    } else if (arg == "-t" || arg == "--test") {
      mode = 't';
    } else if (arg == "-c") {
      // In native cmix, -c is compress mode when 2 files are given.
      // If we see -d already set, -c means to_stdout.
      if (mode == 'd') {
        to_stdout = true;
      } else {
        if (mode == 0) mode = 'c';
      }
    } else if (arg == "--stdout") {
      to_stdout = true;
    } else if (arg == "-n") {
      mode = 'n';
    } else if (arg == "-s") {
      mode = 's';
    } else if (arg == "-f" || arg == "--force") {
      force = true;
    } else if (arg == "-k" || arg == "--keep") {
      keep = true;
    } else if (arg == "-D" && i + 1 < argc) {
      dict_file = argv[++i];
    } else if (arg.rfind("-D", 0) == 0 && arg.length() > 2) {
      dict_file = arg.substr(2);
    } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
      // ignore other flags
    } else {
      positional.push_back(arg);
    }
  }

  // Handle native 4/5 argument syntax: cmix <mode> <dict> <in> <out>
  if (dict_file.empty() && positional.size() == 3 && access(positional[0].c_str(), F_OK) == 0) {
    dict_file = positional[0];
    positional.erase(positional.begin());
  }

  if (mode == 0) {
    mode = 'c';
  }

  FILE* dict = NULL;
  if (!dict_file.empty()) {
    dict = fopen(dict_file.c_str(), "rb");
    if (!dict) {
      fprintf(stderr, "cmix: cannot open dictionary '%s'\n", dict_file.c_str());
      return 1;
    }
    dictionary_path = const_cast<char*>(dict_file.c_str());
  }

  // 1. Archive integrity test mode (-t)
  if (mode == 't') {
    if (positional.empty()) {
      if (dict) fclose(dict);
      return Help(-1);
    }
    std::string archive_path = positional[0];
    char test_template[] = "/tmp/cmix_test_XXXXXX";
    int fd = mkstemp(test_template);
    if (fd == -1) {
      fprintf(stderr, "Error: cannot create temporary file for test\n");
      if (dict) fclose(dict);
      return 1;
    }
    close(fd);
    std::string temp_out = test_template;
    std::string temp_path = temp_out + ".cmix.temp";
    unsigned long long in_b = 0, out_b = 0;
    bool ok = RunDecompression(archive_path, temp_path, temp_out, dict, &in_b, &out_b);
    unlink(test_template);
    if (dict) fclose(dict);
    if (ok) {
      fprintf(stderr, "\nArchive test OK: %s (%llu bytes -> %llu bytes)\n", archive_path.c_str(), in_b, out_b);
      return 0;
    } else {
      fprintf(stderr, "\nArchive test FAILED: %s\n", archive_path.c_str());
      return 1;
    }
  }

  // 2. Pipe mode (stdin / stdout)
  bool is_pipe = false;
  if (positional.empty() && !isatty(fileno(stdin))) {
    is_pipe = true;
  } else if (to_stdout) {
    is_pipe = true;
  } else if (positional.size() == 1 && positional[0] == "-") {
    is_pipe = true;
  } else if (positional.size() >= 2 && positional.back() == "-") {
    is_pipe = true;
  }

  if (is_pipe) {
    std::string in_file;
    bool remove_in = false;

    if (!positional.empty() && positional[0] != "-") {
      in_file = positional[0];
    } else {
      char in_template[] = "/tmp/cmix_pipe_in_XXXXXX";
      int in_fd = mkstemp(in_template);
      if (in_fd == -1) {
        if (dict) fclose(dict);
        return 1;
      }
      char buf[65536];
      ssize_t bytes_read;
      while ((bytes_read = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        if (write(in_fd, buf, bytes_read) != bytes_read) {
          close(in_fd);
          unlink(in_template);
          if (dict) fclose(dict);
          return 1;
        }
      }
      close(in_fd);
      in_file = in_template;
      remove_in = true;
    }

    char out_template[] = "/tmp/cmix_pipe_out_XXXXXX";
    int out_fd = mkstemp(out_template);
    if (out_fd == -1) {
      if (remove_in) unlink(in_file.c_str());
      if (dict) fclose(dict);
      return 1;
    }
    close(out_fd);

    std::string temp_path = std::string(out_template) + ".cmix.temp";
    unsigned long long in_b = 0, out_b = 0;
    bool ok = false;
    if (mode == 'd') {
      ok = RunDecompression(in_file, temp_path, out_template, dict, &in_b, &out_b);
    } else {
      bool enable_preprocess = (mode != 'n');
      bool text_mode = (mode == 's');
      ok = RunCompression(enable_preprocess, text_mode, in_file, temp_path, out_template, dict, &in_b, &out_b);
    }

    if (remove_in) unlink(in_file.c_str());
    if (dict) fclose(dict);

    if (!ok) {
      unlink(out_template);
      return 1;
    }

    int read_out_fd = open(out_template, O_RDONLY);
    if (read_out_fd != -1) {
      char buf[65536];
      ssize_t bytes_read;
      while ((bytes_read = read(read_out_fd, buf, sizeof(buf))) > 0) {
        if (write(STDOUT_FILENO, buf, bytes_read) != bytes_read) break;
      }
      close(read_out_fd);
    }
    unlink(out_template);
    return 0;
  }

  // 3. File arguments mode
  if (positional.empty()) {
    if (dict) fclose(dict);
    return Help(-1);
  }

  std::string input_path = positional[0];
  std::string output_path;

  if (positional.size() == 1) {
    if (mode == 'd') {
      if (input_path.length() > 5 && input_path.substr(input_path.length() - 5) == ".cmix") {
        output_path = input_path.substr(0, input_path.length() - 5);
      } else {
        output_path = input_path + ".out";
      }
    } else {
      output_path = input_path + ".cmix";
    }
  } else {
    output_path = positional[1];
  }

  if (!force && access(output_path.c_str(), F_OK) == 0 && positional.size() == 1) {
    fprintf(stderr, "cmix: output file '%s' already exists (use -f to force)\n", output_path.c_str());
    if (dict) fclose(dict);
    return 1;
  }

  std::string temp_path = output_path + ".cmix.temp";
  unsigned long long in_b = 0, out_b = 0;
  bool ok = false;

  if (mode == 'd') {
    ok = RunDecompression(input_path, temp_path, output_path, dict, &in_b, &out_b);
  } else {
    bool enable_preprocess = (mode != 'n');
    bool text_mode = (mode == 's');
    ok = RunCompression(enable_preprocess, text_mode, input_path, temp_path, output_path, dict, &in_b, &out_b);
  }

  if (dict) fclose(dict);

  if (ok) {
    if (positional.size() == 1 && !keep) {
      unlink(input_path.c_str());
    }
    return 0;
  }
  return 1;
}
