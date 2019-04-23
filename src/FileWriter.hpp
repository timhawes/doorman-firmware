#ifndef FILEWRITER_HPP
#define FILEWRITER_HPP

#include <Arduino.h>
#include <FS.h>

class FileWriter {
 private:
  File file_handle;
  char _filename[15];
  char _md5[33];
  size_t _size = 0;
  bool active = false;
  bool file_open = false;
  unsigned int received_size;
  const char *tmp_filename = "tmp";
  void parse_md5_stream(MD5Builder *md5, Stream *stream);

 public:
  FileWriter();
  bool Begin(const char *filename, const char *md5, size_t size);
  bool UpToDate();
  bool Open();
  bool Add(uint8_t *data, unsigned int len);
  bool Add(uint8_t *data, unsigned int len, unsigned int pos);
  bool Commit();
  void Abort();
  bool Running();
};

#endif
