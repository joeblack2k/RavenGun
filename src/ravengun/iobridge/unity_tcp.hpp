#pragma once
// Dialect "unity-tcp": de daemon als TCP-klant van de BepInEx-plugin in een Unity-game (Tomb Raider, Rabbids Hollywood).
// Dunne laag om AaClient (ravengun/games/aagames.hpp); het protocol en de klant zijn ongewijzigd. Dit is een
// "pull"-dialect: de klantthread haalt de toestand zelf op via de bron (de brug levert dezelfde IoState als voor tp-shm),
// dus er is geen tikthread van de brug nodig.
#include <functional>
#include <string>

#include "ravengun/games/aagames.hpp"
#include "ravengun/iobridge/dialect.hpp"

namespace ravengun::iobridge {

class UnityTcpDialect final : public IoDialect {
public:
    // Bron: vult de toestand; false = niet beschikbaar (uitvoer niet gewapend): alle spelers inactief.
    using Source = std::function<bool(IoState&)>;
    using RecoilFn = std::function<void(int player)>;
    UnityTcpDialect(ravengun::games::AaGame game, int port, Source source, RecoilFn recoil = {});
    ~UnityTcpDialect() override { close(); }
    const char* name() const override { return "unity-tcp"; }
    std::string game() const override { return game_.key; }
    bool open(std::string& err) override;
    void write(const IoState&) override {}
    bool wants_tick() const override { return false; }
    void neutral() override {}
    void close() override;
    IoOutputs outputs() const override;
    std::string status_json() const override;
    ravengun::games::AaClientStats stats() const { return client_.stats(); }
    bool running() const { return client_.running(); }

private:
    ravengun::games::AaGame game_;
    int port_;
    Source source_;
    RecoilFn recoil_;
    mutable ravengun::games::AaClient client_;
};

}  // namespace ravengun::iobridge
