// A janela de escolha de arquivo no macOS: o backend de platform/file_dialog.h com o NSOpenPanel (docs/port.md, "macOS:
// os backends"). Compilado com ARC (-fobjc-arc).
//
// O painel abre na thread principal sem travar o laço do jogo (beginWithCompletionHandler: a tela de entrada continua
// desenhando, como com o IFileOpenDialog numa thread no Windows) e a escolha volta por um estado com uma trava, que take()
// lê na thread do jogo. O que dá para escolher: as imagens de disco (.iso, .cdr: o "DVD/CD master" do Utilitário de
// Disco; .img) e também uma pasta (os arquivos extraídos do disco, ou um volume montado em /Volumes), o que o Windows faz
// pelo leitor e pela linha de comando; o probe_disc confere e recusa com o motivo. O filtro "todos os arquivos" do
// Windows não existe no painel do macOS (FilePickerText::all fica sem uso). O executável da cópia pessoal não se aplica
// aqui (platform/disc_pack.h).

#include "platform/file_dialog.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <memory>
#include <mutex>
#include <string>

#include "platform/main_thread_macos.h"
#include "platform/os.h"

namespace nf {

namespace {

// O estado da escolha, dividido com o bloco do painel (que pode terminar depois do FilePicker).
struct PickState {
    std::mutex lock;
    bool busy = false;
    bool done = false;
    std::wstring path;
    NSOpenPanel* panel = nil;  // só na thread principal
};

NSString* ns_string(const std::string& utf8) {
    NSString* s = [NSString stringWithUTF8String:utf8.c_str()];
    return s != nil ? s : @"";
}

}  // namespace

struct FilePicker::Impl {
    std::shared_ptr<PickState> state = std::make_shared<PickState>();
};

FilePicker::FilePicker() : impl_(std::make_unique<Impl>()) {}

FilePicker::~FilePicker() {
    // O painel aberto fecha como "cancelado" (o bloco dele ainda pode escrever no estado, que é dividido).
    std::shared_ptr<PickState> st = impl_->state;
    if (busy() && macos::can_use_appkit()) {
        macos::post_to_main([st] {
            if (st->panel != nil) [st->panel cancel:nil];
        });
    }
}

bool FilePicker::busy() const {
    std::lock_guard<std::mutex> g(impl_->state->lock);
    return impl_->state->busy;
}

void FilePicker::start(void* owner, const FilePickerText& text) {
    (void)owner;  // o painel é uma janela própria (uma folha presa à janela do jogo não combina com a tela cheia sem bordas)
    std::shared_ptr<PickState> st = impl_->state;
    {
        std::lock_guard<std::mutex> g(st->lock);
        if (st->busy) return;
        st->busy = true;
        st->done = false;
        st->path.clear();
    }
    if (!macos::can_use_appkit()) {
        std::lock_guard<std::mutex> g(st->lock);
        st->busy = false;
        st->done = true;  // sem o AppKit: como cancelado
        return;
    }
    const std::string message = os::to_utf8(text.title);
    macos::post_to_main([st, message] {
        @autoreleasepool {
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = YES;
            panel.canChooseDirectories = YES;
            panel.allowsMultipleSelection = NO;
            panel.message = ns_string(message);
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            for (NSString* ext in @[ @"iso", @"cdr", @"img" ]) {
                UTType* t = [UTType typeWithFilenameExtension:ext];
                if (t != nil) [types addObject:t];
            }
            [types addObject:UTTypeDirectory];
            panel.allowedContentTypes = types;
            st->panel = panel;
            __weak NSOpenPanel* weak = panel;
            [NSApp activateIgnoringOtherApps:YES];
            [panel beginWithCompletionHandler:^(NSModalResponse response) {
              std::wstring chosen;
              NSOpenPanel* p = weak;
              if (response == NSModalResponseOK && p != nil && p.URL != nil && p.URL.path != nil) {
                  chosen = os::from_utf8(p.URL.path.UTF8String);
              }
              {
                  std::lock_guard<std::mutex> g(st->lock);
                  st->path = chosen;
                  st->busy = false;
                  st->done = true;
              }
              st->panel = nil;
            }];
        }
    });
}

bool FilePicker::take(std::wstring* path) {
    std::lock_guard<std::mutex> g(impl_->state->lock);
    if (!impl_->state->done) return false;
    impl_->state->done = false;
    if (path != nullptr) *path = impl_->state->path;
    return true;
}

}  // namespace nf
