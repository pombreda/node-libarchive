#ifndef WRITER_H
#define WRITER_H

#include <string>
#include <node.h>
#include <v8.h>
#include <archive.h>

using namespace v8;
using namespace node;

typedef struct WriteData {
	archive *archive;
	std::string *filename;
	Persistent<Function> callback;
	int result;

	// opts
	mode_t permissions;
	bool atimeIsSet;
	time_t atime;
	bool birthtimeIsSet;
	time_t birthtime;
	bool ctimeIsSet;
	time_t ctime;
	bool mtimeIsSet;
	time_t mtime;

	// files
	size_t bufferSize;
	char *bufferData;

	// symlinks
	std::string *symlink;
} WriteData;

typedef struct CloseData {
	archive *archive;
	Persistent<Function> callback;
	int result;
} CloseData;

class Writer : ObjectWrap {
	public:
		static void Init(Handle<Object> exports);

	private:
		explicit Writer(const char *filename);
		~Writer();

		static Handle<Value> New(const Arguments& args);
		static Persistent<Function> constructor;

		static Handle<Value> WriteFile(const Arguments& args);
		static Handle<Value> WriteDirectory(const Arguments& args);
		static Handle<Value> WriteSymlink(const Arguments& args);
		static Handle<Value> Close(const Arguments& args);

		std::string *filename_;
		archive *archive_;
};

#endif
