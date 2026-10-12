#pragma once
// Registro das funções do jogo que ainda não foram portadas e existem só como stub.
//
// Cada stub chama pending_call com o nome original, para que as ferramentas de verificação
// (nf_loader_check) mostrem o que o código portado tentou usar.

#include <map>
#include <string>

void pending_call(const char* name);
const std::map<std::string, int>& pending_counts();
void pending_reset();
// Só para as ferramentas: volta às contagens guardadas (uma cópia de pending_counts()).
void pending_restore(const std::map<std::string, int>& saved);
