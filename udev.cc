#include <v8.h>
#include <node.h>
#include <nan.h>

#include <libudev.h>

using namespace v8;

static struct udev *udev;

static void PushProperties(Local<Object> obj, struct udev_device* dev) {
    struct udev_list_entry* sysattrs;
    struct udev_list_entry* entry;
    sysattrs = udev_device_get_properties_list_entry(dev);
    udev_list_entry_foreach(entry, sysattrs) {
        const char *name, *value;
        name = udev_list_entry_get_name(entry);
        value = udev_list_entry_get_value(entry);
        if (value != NULL) {
            obj->Set(Nan::New<String>(name).ToLocalChecked(), Nan::New<String>(value).ToLocalChecked());
        } else {
            obj->Set(Nan::New<String>(name).ToLocalChecked(), Nan::Null());
        }
    }
}

class Monitor : public Nan::ObjectWrap {
    struct poll_struct {
        Nan::Persistent<Object> monitor;
    };

    uv_poll_t* poll_handle;
    udev_monitor* mon;
    int fd;

    static void on_handle_event(uv_poll_t* handle, int status, int events) {
        Nan::HandleScope scope;
        poll_struct* data = (poll_struct*)handle->data;
        Local<Object> monitor = Nan::New(data->monitor).ToLocalChecked();
        Monitor* wrapper = Nan::ObjectWrap::Unwrap<Monitor>(monitor);
        udev_device* dev = udev_monitor_receive_device(wrapper->mon);

        Local<Object> obj = Nan::New<Object>();
        obj->Set(Nan::New<String>("syspath").ToLocalChecked(), Nan::New<String>(udev_device_get_syspath(dev).ToLocalChecked()));
        PushProperties(obj, dev);

        TryCatch tc;
        Local<Function> emit = monitor->Get(Nan::New<String>("emit").ToLocalChecked()).As<Function>();
        Local<Value> emitArgs[2];
        emitArgs[0] = Nan::New<String>(udev_device_get_action(dev).ToLocalChecked());
        emitArgs[1] = obj;
        emit->Call(monitor, 2, emitArgs);

        udev_device_unref(dev);
        if (tc.HasCaught()) node::FatalException(tc);
    };

    static NAN_METHOD(New) {
        Nan::HandleScope scope;
        uv_poll_t* handle;
        Monitor* obj = new Monitor();
        obj->Wrap(info.This());
        obj->mon = udev_monitor_new_from_netlink(udev, "udev");
        obj->fd = udev_monitor_get_fd(obj->mon);
        obj->poll_handle = handle = new uv_poll_t;
        udev_monitor_enable_receiving(obj->mon);
        poll_struct* data = new poll_struct;
        data->monitor.Reset(info.This());
        handle->data = data;
        uv_poll_init(uv_default_loop(), obj->poll_handle, obj->fd);
        uv_poll_start(obj->poll_handle, UV_READABLE, on_handle_event);
        NanReturnThis();
    }

    static void on_handle_close(uv_handle_t *handle) {
        poll_struct* data = (poll_struct*)handle->data;
        data->monitor.Reset();
        delete data;
        delete handle;
    }

    static NAN_METHOD(Close) {
        Nan::HandleScope scope;
        Monitor* obj = Nan::ObjectWrap::Unwrap<Monitor>(info.This());
        uv_poll_stop(obj->poll_handle);
        uv_close((uv_handle_t*)obj->poll_handle, on_handle_close);
        udev_monitor_unref(obj->mon);
        return;
    }

    public:
    static void Init(Handle<Object> target) {
        // I do not remember why the functiontemplate was tugged into a persistent.
        static Nan::Persistent<FunctionTemplate> constructor;
        Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
        tpl->SetClassName(Nan::New<String>("Monitor").ToLocalChecked());
        tpl->InstanceTemplate()->SetInternalFieldCount(1);
        Nan::SetPrototypeMethod(tpl, "close", Close);
        constructor.Reset(tpl);
        target->Set(Nan::New<String>("Monitor").ToLocalChecked(), Nan::New(tpl).ToLocalChecked()->GetFunction());
    }
};

NAN_METHOD(List) {
    Nan::HandleScope scope;
    Local<Array> list = Nan::New<Array>();
    struct udev_enumerate* enumerate;
    struct udev_list_entry* devices;
    struct udev_list_entry* entry;
    struct udev_device *dev;

    enumerate = udev_enumerate_new(udev);
    // add match etc. stuff.
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    int i = 0;
    udev_list_entry_foreach(entry, devices) {
        const char *path;
        path = udev_list_entry_get_name(entry);
        dev = udev_device_new_from_syspath(udev, path);
        Local<Object> obj = Nan::New<Object>();
        PushProperties(obj, dev);
        obj->Set(Nan::New<String>("syspath").ToLocalChecked(), Nan::New<String>(path).ToLocalChecked());
        list->Set(i++, obj);
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    info.GetReturnValue().Set(list);
}

static void Init(Handle<Object> target) {
    udev = udev_new();
    if (!udev) {
        Nan::ThrowError("Can't create udev\n");
    }
    target->Set(
        Nan::New<String>("list").ToLocalChecked(),
        Nan::New<FunctionTemplate>(List)->GetFunction());

    Monitor::Init(target);
}
NODE_MODULE(udev, Init)
