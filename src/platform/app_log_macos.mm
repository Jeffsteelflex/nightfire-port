// O registro do programa de janela do macOS (nightfire.log): ver app_log_macos.h. Compilado com ARC (-fobjc-arc).
//
// A thread que copia a saída não aloca (os buffers são globais) nem trava nada que o jogo trave: se a thread do jogo cair
// dentro do operator new (a trava da arena), a cópia ainda leva o relatório de falhas ao registro. O relatório espera por
// ela em wait_drained (dentro do sinal: só ioctl, nanosleep e leituras atômicas).

#include "platform/app_log_macos.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "platform/app_log.h"
#include "platform/crash_report.h"
#include "platform/os.h"

namespace nf::macos {

namespace {

// O estado é global: o relatório de falhas o lê de dentro de um sinal.
int g_log = -1;                                   // o nightfire.log
int g_orig[2] = {-1, -1};                         // para onde a saída padrão e a de erro apontavam antes
std::atomic<int> g_read[2] = {{-1}, {-1}};        // a ponta de leitura de cada cano
std::atomic<bool> g_running{false};
std::atomic<int> g_busy{0};                       // a thread tem algo que leu e ainda não gravou
std::atomic<int> g_stop{0};
pthread_t g_thread;
char g_home[1024];                                // a pasta do usuário, trocada por "~"
size_t g_home_len = 0;

// O que cada saída mandou e ainda não fechou a linha.
struct Pending {
    char text[8192];
    size_t len = 0;
};
Pending g_pending[2];

void write_all(int fd, const char* p, size_t n) {
    while (fd >= 0 && n > 0) {
        const ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return;
        p += w;
        n -= size_t(w);
    }
}

// O último caractere gravado (o fim do registro começa numa linha nova).
char g_last = '\n';

// Um pedaço no registro, com a pasta do usuário trocada.
void log_text(const char* p, size_t n) {
    log_mask_home(p, n, g_home, g_home_len, [](const char* q, size_t m) { write_all(g_log, q, m); });
    if (n > 0) g_last = p[n - 1];
}

__attribute__((format(printf, 1, 2))) void log_line(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) log_text(buf, size_t(n) < sizeof(buf) ? size_t(n) : sizeof(buf) - 1);
}

// A saída que gravou por último (-1: o cabeçalho).
int g_last_stream = -1;

// Uma linha (ou o começo dela, com o cano vazio) no registro. Se a outra saída deixou uma linha sem o fim, esta começa
// numa linha nova: as duas saídas não se misturam numa linha só.
void flush_pending(int s) {
    Pending& pd = g_pending[s];
    if (pd.len > 0) {
        if (g_last != '\n' && g_last_stream != s) log_text("\n", 1);
        log_text(pd.text, pd.len);
        g_last_stream = s;
    }
    pd.len = 0;
}

// O que chegou de uma saída: como está para onde ela apontava antes; linha a linha para o registro.
void pass(int s, const char* p, size_t n) {
    write_all(g_orig[s], p, n);
    Pending& pd = g_pending[s];
    for (size_t i = 0; i < n; ++i) {
        pd.text[pd.len++] = p[i];
        if (p[i] == '\n' || pd.len == sizeof(pd.text)) flush_pending(s);
    }
}

size_t pending_bytes(int fd) {
    int n = 0;
    return fd >= 0 && ioctl(fd, FIONREAD, &n) == 0 && n > 0 ? size_t(n) : 0;
}

// A thread da cópia: espera os dois canos, lê tudo o que houver (as pontas de leitura não bloqueiam) e grava. Uma linha
// sem o fim vai ao registro quando o cano fica vazio (o relatório de falhas não espera por um '\n'). Acaba quando os dois
// canos fecham (as saídas voltaram) ou, com g_stop, na primeira volta sem nada.
void* pump(void*) {
    char buf[4096];
    for (;;) {
        pollfd fds[2];
        nfds_t count = 0;
        for (int s = 0; s < 2; ++s) {
            const int fd = g_read[s].load();
            if (fd < 0) continue;
            fds[count].fd = fd;
            fds[count].events = POLLIN;
            fds[count].revents = 0;
            ++count;
        }
        if (count == 0) break;
        if (poll(fds, count, 200) < 0 && errno != EINTR) break;
        g_busy.store(1);
        bool got = false;
        for (int s = 0; s < 2; ++s) {
            const int fd = g_read[s].load();
            if (fd < 0) continue;
            for (;;) {
                const ssize_t n = read(fd, buf, sizeof(buf));
                if (n > 0) {
                    pass(s, buf, size_t(n));
                    got = true;
                    continue;
                }
                if (n < 0 && errno == EINTR) continue;
                if (n == 0) {  // o fim: a saída voltou para onde estava
                    flush_pending(s);
                    g_read[s].store(-1);
                    close(fd);
                }
                break;  // vazio (EAGAIN) ou erro
            }
            if (g_read[s].load() >= 0 && pending_bytes(fd) == 0) flush_pending(s);
        }
        g_busy.store(0);
        if (g_stop.load() != 0 && !got) break;
    }
    flush_pending(0);
    flush_pending(1);
    return nullptr;
}

// Antes de o processo acabar (uma falha, os::exit_process): espera a thread levar ao registro o que ainda está nos
// canos, no máximo 1 s (a falha pode ser na própria thread). Roda dentro de um sinal.
void wait_drained() {
    if (!g_running.load()) return;
    for (int i = 0; i < 100; ++i) {
        const size_t left = pending_bytes(g_read[0].load()) + pending_bytes(g_read[1].load());
        if (left == 0 && g_busy.load() == 0) return;
        timespec ts{0, 10 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
}

std::string sysctl_text(const char* name) {
    char buf[256];
    size_t size = sizeof(buf);
    if (sysctlbyname(name, buf, &size, nullptr, 0) != 0 || size == 0) return "?";
    buf[sizeof(buf) - 1] = 0;
    return std::string(buf);
}

int64_t sysctl_int(const char* name) {
    int64_t v64 = 0;
    size_t size = sizeof(v64);
    if (sysctlbyname(name, &v64, &size, nullptr, 0) != 0) return -1;
    if (size == sizeof(int32_t)) {
        int32_t v32 = 0;
        std::memcpy(&v32, &v64, sizeof(v32));
        return v32;
    }
    return v64;
}

const char* binary_arch() {
#if defined(__aarch64__)
    return "arm64";
#elif defined(__x86_64__)
    return "x86_64";
#else
    return "?";
#endif
}

// O cabeçalho: a versão, a máquina e como o programa foi aberto (em inglês, como o guia: quem testa o lê e o anexa).
void write_header(const char* build, int argc, char** argv, const std::string& path) {
    @autoreleasepool {
        NSDictionary* info = [[NSBundle mainBundle] infoDictionary];
        NSString* version = info[@"CFBundleShortVersionString"];
        NSString* plist_build = info[@"NFPortBuild"];
        const char* code_build = build != nullptr && *build != 0 ? build
                                 : plist_build != nil         ? plist_build.UTF8String
                                                              : "?";
        // sysctl.proc_translated: 1 com o Rosetta (um binário x86_64 num Mac com Apple Silicon); não existe num Intel.
        const bool rosetta = sysctl_int("sysctl.proc_translated") == 1;
        log_line("Nightfire PC %s (build %s), %s%s\n", version != nil ? version.UTF8String : "dev", code_build,
                 binary_arch(), rosetta ? " under Rosetta" : "");
        char date[64] = "?";
        const time_t now = time(nullptr);
        struct tm local {};
        if (localtime_r(&now, &local) != nullptr) strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S %z", &local);
        log_line("Started: %s\n", date);
        log_line("macOS: %s\n", [[NSProcessInfo processInfo] operatingSystemVersionString].UTF8String);
        const int64_t mem = sysctl_int("hw.memsize");
        log_line("Mac: %s, %s, %lld CPUs, %.1f GB\n", sysctl_text("hw.model").c_str(),
                 sysctl_text("machdep.cpu.brand_string").c_str(), (long long)sysctl_int("hw.ncpu"),
                 mem > 0 ? double(mem) / double(1ull << 30) : 0.0);
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
        if (devices.count == 0) log_line("GPU: none listed by Metal\n");
        for (id<MTLDevice> d in devices) {
            log_line("GPU: %s%s%s%s, %llu MB recommended\n", d.name.UTF8String, d.hasUnifiedMemory ? ", unified memory" : "",
                     d.lowPower ? ", low power" : "", d.removable ? ", external" : "",
                     (unsigned long long)(d.recommendedMaxWorkingSetSize >> 20));
        }
        log_line("App: %s\n", os::to_utf8(os::exe_path()).c_str());
        std::string args;
        for (int i = 1; i < argc; ++i) {
            const char* a = argv[i] != nullptr ? argv[i] : "";
            args += ' ';
            const bool quote = *a == 0 || std::strchr(a, ' ') != nullptr;
            if (quote) args += '"';
            args += a;
            if (quote) args += '"';
        }
        log_line("Arguments:%s\n", args.empty() ? " (none)" : args.c_str());
        log_line("Log: %s\n", path.c_str());
        log_line("----\n");
    }
}

void at_exit() { stop_app_log(INT_MIN); }

// As saídas 0 a 2 abertas: um processo aberto com uma delas fechada daria o número dela a um dos canos.
void ensure_std_fds() {
    for (int fd = 0; fd <= 2; ++fd) {
        if (fcntl(fd, F_GETFD) != -1) continue;
        const int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0 && null_fd != fd) {
            dup2(null_fd, fd);
            close(null_fd);
        }
    }
}

}  // namespace

std::wstring start_app_log(const std::wstring& dir_w, const char* build, int argc, char** argv) {
    if (g_running.load() || g_log >= 0) return {};
    std::string dir = os::to_utf8(dir_w);
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    if (dir.empty()) return {};
    const std::string path = dir + "/nightfire.log";
    const std::string prev = dir + "/nightfire.prev.log";
    std::rename(path.c_str(), prev.c_str());  // o da partida anterior (sem ele, nada)
    g_log = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (g_log < 0) return {};
    g_last = '\n';
    g_home_len = 0;
    if (const char* home = std::getenv("HOME")) {
        size_t n = std::strlen(home);
        while (n > 1 && home[n - 1] == '/') --n;
        if (n >= 2 && n < sizeof(g_home)) {
            std::memcpy(g_home, home, n);
            g_home_len = n;
        }
    }
    write_header(build, argc, argv, path);

    ensure_std_fds();
    int out[2] = {-1, -1};
    int err[2] = {-1, -1};
    if (pipe(out) != 0 || pipe(err) != 0) {
        for (int fd : {out[0], out[1], err[0], err[1]}) {
            if (fd >= 0) close(fd);
        }
        log_line("(the output is not copied here: no pipe)\n");
        return os::from_utf8(path);
    }
    for (int fd : {out[0], err[0]}) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    g_orig[0] = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    g_orig[1] = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
    std::fflush(stdout);
    std::fflush(stderr);
    dup2(out[1], STDOUT_FILENO);
    dup2(err[1], STDERR_FILENO);
    close(out[1]);
    close(err[1]);
    g_read[0].store(out[0]);
    g_read[1].store(err[0]);
    g_stop.store(0);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, size_t(256) << 10);
    const bool started = pthread_create(&g_thread, &attr, pump, nullptr) == 0;
    pthread_attr_destroy(&attr);
    if (!started) {
        // Sem a thread: as saídas voltam e o registro fica só com o cabeçalho.
        if (g_orig[0] >= 0) dup2(g_orig[0], STDOUT_FILENO);
        if (g_orig[1] >= 0) dup2(g_orig[1], STDERR_FILENO);
        for (int s = 0; s < 2; ++s) {
            close(g_read[s].exchange(-1));
            if (g_orig[s] >= 0) close(g_orig[s]);
            g_orig[s] = -1;
        }
        log_line("(the output is not copied here: no thread)\n");
        return os::from_utf8(path);
    }
    g_running.store(true);
    crash_report_set_flush(wait_drained);
    static bool registered = false;
    if (!registered) {
        registered = true;
        std::atexit(at_exit);
    }
    return os::from_utf8(path);
}

void stop_app_log(int exit_code) {
    if (g_running.load()) {
        std::fflush(stdout);
        std::fflush(stderr);
        wait_drained();
        crash_report_set_flush(nullptr);
        // As saídas voltam para onde estavam: os canos fecham e a thread lê o fim deles.
        for (int s = 0; s < 2; ++s) {
            const int target = s == 0 ? STDOUT_FILENO : STDERR_FILENO;
            if (g_orig[s] >= 0) {
                dup2(g_orig[s], target);
            } else {
                const int null_fd = open("/dev/null", O_WRONLY);
                if (null_fd >= 0) {
                    dup2(null_fd, target);
                    close(null_fd);
                }
            }
        }
        g_stop.store(1);
        pthread_join(g_thread, nullptr);
        g_running.store(false);
        for (int s = 0; s < 2; ++s) {
            const int fd = g_read[s].exchange(-1);
            if (fd >= 0) close(fd);
            if (g_orig[s] >= 0) close(g_orig[s]);
            g_orig[s] = -1;
        }
    }
    if (g_log >= 0) {
        if (g_last != '\n') log_text("\n", 1);
        if (exit_code != INT_MIN) log_line("---- end (exit code %d)\n", exit_code);
        else log_line("---- end\n");
        close(g_log);
        g_log = -1;
    }
}

}  // namespace nf::macos
