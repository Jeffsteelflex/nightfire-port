// A janela de escolha de arquivo do Windows (IFileOpenDialog) numa thread STA própria. Ver file_dialog.h.

#include "platform/file_dialog.h"

#include <windows.h>
#include <shobjidl.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace nf {

struct FilePicker::Impl {
    std::thread thread;
    std::atomic<int> state{0};  // 0 nada, 1 aberta, 2 respondida
    std::mutex mutex;
    std::wstring result;
};

FilePicker::FilePicker() : impl_(std::make_unique<Impl>()) {}

FilePicker::~FilePicker() {
    if (impl_->thread.joinable()) impl_->thread.join();
}

bool FilePicker::busy() const { return impl_->state == 1; }

void FilePicker::start(void* owner_window, const FilePickerText& text) {
    if (busy()) return;
    Impl* m = impl_.get();
    if (m->thread.joinable()) m->thread.join();
    m->state = 1;
    const HWND owner = static_cast<HWND>(owner_window);
    const std::wstring title = text.title;
    const std::wstring images = text.images;
    const std::wstring all = text.all;
    m->thread = std::thread([m, owner, title, images, all] {
        std::wstring path;
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
            IFileOpenDialog* d = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
                const COMDLG_FILTERSPEC filters[] = {{images.c_str(), L"*.iso"}, {all.c_str(), L"*.*"}};
                d->SetFileTypes(2, filters);
                d->SetTitle(title.c_str());
                DWORD flags = 0;
                d->GetOptions(&flags);
                d->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
                if (SUCCEEDED(d->Show(owner))) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(d->GetResult(&item))) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p != nullptr) {
                            path = p;
                            CoTaskMemFree(p);
                        }
                        item->Release();
                    }
                }
                d->Release();
            }
            CoUninitialize();
        }
        {
            std::lock_guard<std::mutex> lock(m->mutex);
            m->result = path;
        }
        m->state = 2;
    });
}

bool FilePicker::take(std::wstring* path) {
    if (impl_->state != 2) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    *path = impl_->result;
    impl_->state = 0;
    return true;
}

}  // namespace nf
