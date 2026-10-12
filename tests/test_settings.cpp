// Testes da configuração do port (platform/settings.cpp): leitura, valores padrão e regravação que
// mantém comentários, ordem e chaves desconhecidas.

#include <string>

#include "platform/settings.h"
#include "test_framework.h"

TEST(settings_read_values_and_fallbacks) {
    nf::Settings s;
    s.parse("; comentário\r\n[Video]\r\nwidth = 2560\r\nscale=1.5\r\nvsync = on\r\n\r\n[ui]\r\nhd = 0\r\n");
    CHECK(s.get_int("video", "WIDTH", 0) == 2560);
    CHECK(s.get_float("Video", "scale", 1.0f) == 1.5f);
    CHECK(s.get_bool("video", "vsync", false));
    CHECK(!s.get_bool("ui", "hd", true));
    CHECK(s.get_int("video", "height", 1080) == 1080);  // ausente: o padrão
    CHECK(s.get("controls", "x", "nada") == "nada");
    CHECK(s.has("video", "width") && !s.has("ui", "width"));
}

TEST(settings_rewrite_keeps_comments_and_order) {
    nf::Settings s;
    const std::string text = "; topo\r\n[video]\r\n# largura\r\nwidth = 1920\r\nheight = 1080\r\n\r\n[ui]\r\nhd = 1\r\n";
    s.parse(text);
    CHECK(s.text() == text);  // sem mudança, o texto volta igual
    s.set_int("video", "width", 2560);
    s.set_bool("video", "vsync", true);  // chave nova: no fim da seção, antes da linha vazia
    s.set_float("controls", "mouse", 0.25f);  // seção nova no fim
    CHECK(s.text() ==
          "; topo\r\n[video]\r\n# largura\r\nwidth = 2560\r\nheight = 1080\r\nvsync = 1\r\n\r\n[ui]\r\nhd = 1\r\n"
          "\r\n[controls]\r\nmouse = 0.25\r\n");
    s.remove("video", "height");
    CHECK(!s.has("video", "height") && s.get_int("video", "width", 0) == 2560);
    // Lido de novo, dá os mesmos valores.
    nf::Settings t;
    t.parse(s.text());
    CHECK(t.get_float("controls", "mouse", 0.0f) == 0.25f && t.get_bool("video", "vsync", false));
}

TEST(settings_numbers_and_bad_values) {
    nf::Settings s;
    s.parse("[a]\nhex = 0x10\nneg = -3\nbad = abc\nflag = talvez\n");
    CHECK(s.get_int("a", "hex", 0) == 16 && s.get_int("a", "neg", 0) == -3);
    CHECK(s.get_int("a", "bad", 7) == 7 && s.get_float("a", "bad", 2.5f) == 2.5f);
    CHECK(s.get_bool("a", "flag", true));  // valor que não é sim/não: o padrão
    CHECK(s.dirty());
}
