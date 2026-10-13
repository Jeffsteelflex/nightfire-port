#pragma once
// O sistema de atributos do DRIVING.ELF (AttributeSystem, AttributeSet): os valores com nome que as classes do jogo
// consultam (o manejo de cada veículo, os objetos quebráveis, o mundo de cada missão). No disco, dentro do .VIV de cada
// missão: data\sim\attrib\attrib.dir lista as coleções ("classe\nome") e cada uma é data\sim\attrib\<classe>\<nome>.atr,
// um INI (lido pelo DAFI) com linhas "CHAVE[.tipo]=valor".
//
// Port: um leitor próprio com as regras do jogo (AttributeSystem::LoadCollection 0x57ee0 e ProcessDafiSection 0x57a80
// do Xbox, lidos no nightfire-cxbx, NightfireResearch): a seção da pista, a da própria coleção (salvo a "default") e a
// "[default]" do arquivo, nessa ordem, cada chave de uma seção posterior sobrescrevendo a anterior; o sufixo de tipo
// (".f", ".i", ".s"...) sai do nome; uma chave que falta vai à coleção "default" da classe (a mãe). As chaves de campo
// da estrutura de extensão "CarPhysics" da classe "pvehicle" (as de PVehicle::InitializeGlobals 0x1968f8) preenchem a
// estrutura da coleção, criada zerada (a mãe só a empresta se a coleção não tiver nenhuma dessas chaves). Os números:
// strtod e a conversão para float (o analisador do jogo, 0x16c1d0 para float e 0x16c010 para inteiro, não foi
// portado: a conferir nos casos de borda).

#include <map>
#include <memory>
#include <string>

#include "driving/physics/rigid_body.h"

namespace drv {

class Bigf;

class AttributeCollection {
public:
    // Lê data\sim\attrib\<cls>\<name>.atr do arquivo (e a "default" da classe como mãe, se name não for ela).
    // track_section: a seção lida antes da própria (AttributeSystem +0x20; o nome da pista).
    bool load(const Bigf& arc, const std::string& cls, const std::string& name, const std::string& track_section,
              std::string* error = nullptr);
    const std::string& name() const { return name_; }
    // O valor (o texto) da chave na coleção ou na mãe.
    bool find(const std::string& key, std::string& value) const;
    // AttributeSet::LookupFloat/LookupInt/LookupBool: 0 (falso) quando falta.
    float lookup_float(const std::string& key, bool* found = nullptr) const;
    int32_t lookup_int(const std::string& key, bool* found = nullptr) const;
    bool lookup_bool(const std::string& key, bool* found = nullptr) const;
    // AttributeSet::LookupStruct("CarPhysics"): a estrutura da coleção (ou da mãe); zerada sem nenhum campo.
    const CarPhysics& car_physics() const;
    int keys() const { return int(values_.size()); }

private:
    std::string name_;
    std::map<std::string, std::string> values_;
    CarPhysics physics_{};
    bool has_physics_ = false;
    std::shared_ptr<AttributeCollection> parent_;
    friend class AttributeLoader;
};

}  // namespace drv
