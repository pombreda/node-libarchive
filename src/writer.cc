#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <archive.h>
#include <archive_entry.h>
#include "writer.h"

using namespace v8;
using namespace node;

Persistent<Function> Writer::constructor;

Writer::Writer(const char *filename) {
	filename_ = new std::string(filename);

	archive_ = archive_write_new();
	archive_write_set_format_zip(archive_);
	archive_write_add_filter_none(archive_);
	archive_write_open_filename(archive_, filename);
}

Writer::~Writer() {
	archive_write_free(archive_);
	delete filename_;
}

void Writer::Init(Handle<Object> exports) {
	Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
	tpl->SetClassName(String::NewSymbol("Writer"));
	tpl->InstanceTemplate()->SetInternalFieldCount(1);

	tpl->PrototypeTemplate()->Set(String::NewSymbol("writeFile"), FunctionTemplate::New(WriteFile)->GetFunction());
	tpl->PrototypeTemplate()->Set(String::NewSymbol("writeDirectory"), FunctionTemplate::New(WriteDirectory)->GetFunction());
	tpl->PrototypeTemplate()->Set(String::NewSymbol("writeSymlink"), FunctionTemplate::New(WriteSymlink)->GetFunction());
	tpl->PrototypeTemplate()->Set(String::NewSymbol("close"), FunctionTemplate::New(Close)->GetFunction());

	constructor = Persistent<Function>::New(tpl->GetFunction());
	exports->Set(String::NewSymbol("Writer"), constructor);
}

Handle<Value> Writer::New(const Arguments& args) {
	HandleScope scope;

	if (args.IsConstructCall()) {
		if (args.Length() < 1) {
			ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
			return scope.Close(Undefined());
		}

		Writer* obj = new Writer(*String::Utf8Value(args[0]));
		obj->Wrap(args.This());
		args.This()->Set(String::New("filename"), args[0]);

		return args.This();
	} else {
		const int argc = 0;
		Local<Value> argv[argc] = { };
		return scope.Close(constructor->NewInstance(argc, argv));
	}
}

void OnWrite(uv_work_t *req) {
	WriteData *data = (WriteData*) req->data;
	
	int argc = 1;
	Handle<Value> argv[] = { Null() };

	if (data->result != ARCHIVE_OK) {
		argv[0] = Exception::Error(String::Concat(String::New("Could not write to archive: "), String::New(data->filename->c_str())));
	}

	TryCatch tryCatch;
	data->callback->Call(Context::GetCurrent()->Global(), argc, argv);
	if (tryCatch.HasCaught()) {
		node::FatalException(tryCatch);
	}

	data->archive = NULL;
	data->callback.Dispose();

	if (NULL != data->symlink) {
		delete data->symlink;
	}

	delete data->filename;
	delete data;
	delete req;
}

void DoWriteFile(uv_work_t *req) {
	WriteData *data = (WriteData*) req->data;

	archive_entry *entry = archive_entry_new();
	archive_entry_set_pathname(entry, data->filename->c_str());
	archive_entry_set_size(entry, data->bufferSize);
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_perm(entry, data->permissions);

	if (ARCHIVE_OK != (data->result = archive_write_header(data->archive, entry))) {
		return;
	}

	if ((data->result = archive_write_data(data->archive, data->bufferData, data->bufferSize)) < 0) {
		return;
	}

	data->result = ARCHIVE_OK;
  archive_entry_free(entry);
}

// (filename, permissions, buffer, cb)
Handle<Value> Writer::WriteFile(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() != 4) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}
	
	if (!args[0]->IsString() || !args[1]->IsNumber() || !Buffer::HasInstance(args[2]) || !args[3]->IsFunction()) {
		ThrowException(Exception::TypeError(String::New("Wrong arguments")));
		return scope.Close(Undefined());
	}
	
	Writer *me = ObjectWrap::Unwrap<Writer>(args.This());
	uv_work_t *req = new uv_work_t();
	WriteData *data = new WriteData();
	
	req->data = data;

	data->archive = me->archive_;
	data->filename = new std::string(*String::Utf8Value(args[0]));
	data->permissions = args[1]->NumberValue();
	data->bufferSize = Buffer::Length(args[2]);
	data->bufferData = Buffer::Data(args[2]);
	data->callback = Persistent<Function>::New(Local<Function>::Cast(args[3]));
	data->result = ARCHIVE_OK;
	
	uv_queue_work(uv_default_loop(), req, DoWriteFile, (uv_after_work_cb) OnWrite);
	
	return scope.Close(Undefined());
}

void DoWriteDirectory(uv_work_t *req) {
	WriteData *data = (WriteData*) req->data;

	archive_entry *entry = archive_entry_new();
	archive_entry_set_pathname(entry, data->filename->c_str());
	archive_entry_set_filetype(entry, AE_IFDIR);
	archive_entry_set_perm(entry, data->permissions);

	if (ARCHIVE_OK != (data->result = archive_write_header(data->archive, entry))) {
		return;
	}

	data->result = ARCHIVE_OK;
  archive_entry_free(entry);
}

// (filename, permissions, cb)
Handle<Value> Writer::WriteDirectory(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() != 3) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}
	
	if (!args[0]->IsString() || !args[1]->IsNumber() || !args[2]->IsFunction()) {
		ThrowException(Exception::TypeError(String::New("Wrong arguments")));
		return scope.Close(Undefined());
	}
	
	Writer *me = ObjectWrap::Unwrap<Writer>(args.This());
	uv_work_t *req = new uv_work_t();
	WriteData *data = new WriteData();
	
	req->data = data;

	data->archive = me->archive_;
	data->filename = new std::string(*String::Utf8Value(args[0]));
	data->permissions = args[1]->NumberValue();
	data->callback = Persistent<Function>::New(Local<Function>::Cast(args[2]));
	data->result = ARCHIVE_OK;
	
	uv_queue_work(uv_default_loop(), req, DoWriteDirectory, (uv_after_work_cb) OnWrite);
	
	return scope.Close(Undefined());
}

void DoWriteSymlink(uv_work_t *req) {
	WriteData *data = (WriteData*) req->data;

	archive_entry *entry = archive_entry_new();
	archive_entry_set_pathname(entry, data->filename->c_str());
	archive_entry_set_filetype(entry, AE_IFLNK);
	archive_entry_set_perm(entry, data->permissions);
	archive_entry_set_symlink(entry, data->symlink->c_str());

	if (ARCHIVE_OK != (data->result = archive_write_header(data->archive, entry))) {
		return;
	}

	data->result = ARCHIVE_OK;
  archive_entry_free(entry);
}

// (filename, permissions, symlink, cb)
Handle<Value> Writer::WriteSymlink(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() != 4) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}
	
	if (!args[0]->IsString() || !args[1]->IsNumber() || !args[2]->IsString() || !args[3]->IsFunction()) {
		ThrowException(Exception::TypeError(String::New("Wrong arguments")));
		return scope.Close(Undefined());
	}
	
	Writer *me = ObjectWrap::Unwrap<Writer>(args.This());
	uv_work_t *req = new uv_work_t();
	WriteData *data = new WriteData();
	
	req->data = data;

	data->archive = me->archive_;
	data->filename = new std::string(*String::Utf8Value(args[0]));
	data->permissions = args[1]->NumberValue();
	data->symlink = new std::string(*String::Utf8Value(args[2]));
	data->callback = Persistent<Function>::New(Local<Function>::Cast(args[3]));
	data->result = ARCHIVE_OK;
	
	uv_queue_work(uv_default_loop(), req, DoWriteSymlink, (uv_after_work_cb) OnWrite);
	
	return scope.Close(Undefined());
}

void DoClose(uv_work_t *req) {
	CloseData *data = (CloseData*) req->data;
  data->result = archive_write_close(data->archive);
}

void OnClose(uv_work_t *req) {
	CloseData *data = (CloseData*) req->data;
	
	int argc = 1;
	Handle<Value> argv[] = { Null() };

	if (data->result != ARCHIVE_OK) {
		argv[0] = Exception::Error(String::New("Could not write archive"));
	}

	TryCatch tryCatch;
	data->callback->Call(Context::GetCurrent()->Global(), argc, argv);
	if (tryCatch.HasCaught()) {
		node::FatalException(tryCatch);
	}

	data->archive = NULL;
	data->callback.Dispose();
	delete data;
	delete req;
}

Handle<Value> Writer::Close(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() != 1) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}
	
	if (!args[0]->IsFunction()) {
		ThrowException(Exception::TypeError(String::New("Wrong arguments")));
		return scope.Close(Undefined());
	}

	Writer *me = ObjectWrap::Unwrap<Writer>(args.This());
	uv_work_t *req = new uv_work_t();
	CloseData *data = new CloseData();
	
	req->data = data;

	data->archive = me->archive_;
	data->callback = Persistent<Function>::New(Local<Function>::Cast(args[0]));
	data->result = ARCHIVE_OK;
	
	uv_queue_work(uv_default_loop(), req, DoClose, (uv_after_work_cb) OnClose);
	
	return scope.Close(Undefined());
}