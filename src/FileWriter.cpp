#include "FileWriter.hpp"

FileWriter::FileWriter() {
  strncpy(_filename, "", sizeof(_filename));
  strncpy(_md5, "", sizeof(_md5));
  _size = 0;
}

void FileWriter::Abort() {
  if (file_handle) {
    file_handle.close();
  }
  SPIFFS.remove(tmp_filename);
  strncpy(_filename, "", sizeof(_filename));
  strncpy(_md5, "", sizeof(_md5));
  _size = 0;
  file_open = false;
  active = false;
}

bool FileWriter::Begin(const char *filename, const char *md5, size_t size) {
  if (active) {
    Serial.println("FileWriter: begin(): aborting existing task first");
    abort();
  }
  active = true;
  strncpy(_filename, filename, sizeof(_filename));
  strncpy(_md5, md5, sizeof(_md5));
  _size = size;
  return true;
}

bool FileWriter::Add(uint8_t *data, unsigned int len) {
  if (file_open) {
    received_size += len;
    return file_handle.write(data, len);
  } else {
    return false;
  }
}

bool FileWriter::Add(uint8_t *data, unsigned int len, unsigned int pos) {
  if (file_open) {
    if (file_handle.seek(pos, SeekSet)) {
      received_size += len;
      return file_handle.write(data, len);
    } else {
      return false;
    }
  } else {
    return false;
  }
}

bool FileWriter::Commit() {
  if (file_handle) {
    file_handle.close();
    file_open = false;

    MD5Builder tmp_md5;
    File tmp_file = SPIFFS.open(tmp_filename, "r");
    size_t tmp_file_size = tmp_file.size();
    tmp_md5.begin();
    while (tmp_file.available()) {
      uint8_t buf[256];
      size_t buflen;
      buflen = tmp_file.readBytes((char *)buf, 256);
      tmp_md5.add(buf, buflen);
    }
    tmp_md5.calculate();
    tmp_file.close();

    Serial.print("FileWriter: advertised: md5=");
    Serial.print(_md5);
    Serial.print(" size=");
    Serial.println(_size, DEC);

    Serial.print("FileWriter: commit: md5=");
    Serial.print(tmp_md5.toString());
    Serial.print(" size=");
    Serial.print(tmp_file_size, DEC);

    if (_size == tmp_file_size &&
        strcmp(tmp_md5.toString().c_str(), _md5) == 0) {
      Serial.println(" match");
      SPIFFS.remove(_filename);
      SPIFFS.rename(tmp_filename, _filename);
      active = false;
      return true;
    } else {
      Serial.println(" mismatch!");
      Abort();
      return false;
    }
  } else {
    return false;
  }
}

bool FileWriter::Open() {
  file_handle = SPIFFS.open(tmp_filename, "w");
  if (file_handle) {
    received_size = 0;
    file_open = true;
    active = true;
    Serial.println("FileWriter: file opened");
    return true;
  } else {
    file_open = false;
    Serial.println("FileWriter: file not opened");
    return false;
  }
}

bool FileWriter::Running() {
  return active;
}

bool FileWriter::UpToDate() {
  MD5Builder md5;
  File f = SPIFFS.open(_filename, "r");
  size_t size = f.size();
  parse_md5_stream(&md5, &f);
  f.close();

  Serial.print("FileWriter: file offered local=");
  Serial.print(size, DEC);
  Serial.print("/");
  Serial.print(md5.toString().c_str());
  Serial.print(" remote=");
  Serial.print(_size, DEC);
  Serial.print("/");
  Serial.print(_md5);

  if (size == _size && strcmp(md5.toString().c_str(), _md5) == 0) {
    Serial.println(" [ok]");
    return true;
  } else {
    Serial.println(" [changed]");
    return false;
  }
}

void FileWriter::parse_md5_stream(MD5Builder *md5, Stream *stream) {
  md5->begin();
  while (stream->available()) {
    uint8_t buf[256];
    size_t buflen;
    buflen = stream->readBytes((char *)buf, 256);
    md5->add(buf, buflen);
  }
  md5->calculate();
}
