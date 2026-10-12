// Textos só do port, em português e em inglês. Ver pc_text.h.

#include "engine/pc_text.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "engine/font.h"

namespace {

struct Entry {
    const char* pt;
    const char* en;
};

// Na ordem de PcText. A linha da pausa segue o estilo da lista da pausa ("C O N T I N U E": maiúsculas
// espaçadas); as abas, o das abas da pausa (MISSION, OBJECTIVES...); as linhas, o das páginas de opções do
// menu principal ("Vibration", "Music Volume": só a inicial maiúscula).
const Entry kTable[] = {
    {"O P Ç Õ E S", "O P T I O N S"},
    {"VÍDEO", "VIDEO"},
    {"GRÁFICOS", "GRAPHICS"},
    {"INTERFACE", "INTERFACE"},
    {"ÁUDIO", "AUDIO"},
    {"CONTROLES", "CONTROLS"},
    {"TECLAS", "KEYS"},
    {"~H Aba  ~V Opções  ~B Voltar", "~H Tab  ~V Options  ~B Back"},
    {"~A Escolher  ~V Rolar  ~H Mudar  ~B Voltar", "~A Select  ~V Scroll  ~H Change  ~B Back"},
    {"Resolução", "Resolution"},
    {"Modo de tela", "Display mode"},
    {"Sincronia vertical", "VSync"},
    {"Campo de visão  %d", "Field of view  %d"},
    {"Escala da renderização", "Render scale"},
    {"Anti-aliasing", "Anti-aliasing"},
    {"Filtro de texturas", "Texture filtering"},
    {"Texturas HD", "HD textures"},
    {"DLSS / DLAA", "DLSS / DLAA"},
    {"Interface HD", "HD interface"},
    {"Posição do HUD", "HUD position"},
    {"Idioma das opções", "Options language"},
    {"Aplicar mudanças", "Apply changes"},
    {"Janela", "Windowed"},
    {"Janela sem bordas", "Borderless"},
    {"Tela cheia exclusiva", "Exclusive fullscreen"},
    {"Desligado", "Off"},
    {"Ligado", "On"},
    {"Bilinear", "Bilinear"},
    {"Trilinear", "Trilinear"},
    {"Anisotrópico %dx", "Anisotropic %dx"},
    {"MSAA %dx", "MSAA %dx"},
    {"Centro (original)", "Centre (original)"},
    {"Bordas da tela", "Screen edges"},
    {"Português", "English"},
    {"Em breve", "Coming soon"},
    {"%dx%d (monitor)", "%dx%d (desktop)"},
    {"Manter estas configurações de vídeo?\nVoltando às anteriores em %d s.",
     "Keep these video settings?\nReverting in %d s."},
    {"SIM", "YES"},
    {"NÃO", "NO"},
    {"Volume da música", "Music volume"},
    {"Volume dos efeitos", "Effects volume"},
    {"Som e música", "Sound and music"},
    {"Dispositivo", "Device"},
    {"Teclado e mouse", "Keyboard and mouse"},
    {"Controle %d", "Controller %d"},
    {"Teclado (controle %d)", "Keyboard (pad %d)"},
    {"Sensib. do mouse  %.2f", "Mouse speed  %.2f"},
    {"Mouse mirando  %.2fx", "Mouse aiming  %.2fx"},
    {"Inverter Y do mouse", "Invert mouse Y"},
    {"Sensib. controle  %.1fx", "Pad speed  %.1fx"},
    {"Zona morta  %d%%", "Dead zone  %d%%"},
    {"Inverter Y do controle", "Invert controller Y"},
    {"Vibração", "Vibration"},
    {"Gatilhos = R1/L1", "Triggers = R1/L1"},
    {"~A Escolher  ~V Rolar  ~H Ajustar  ~B Voltar", "~A Select  ~V Scroll  ~H Adjust  ~B Back"},
    {"~A Trocar  ~V Rolar  ~B Voltar  Del limpa", "~A Rebind  ~V Scroll  ~B Back  Del clears"},
    {"RESTAURAR PADRÕES", "RESTORE DEFAULTS"},
    {"%s: aperte a tecla ou botão (Esc cancela)", "%s: press a key or button (Esc cancels)"},
    {"Cancelado", "Cancelled"},
    {"%s: teclado limpo", "%s: keyboard cleared"},
    {"%s -> %s", "%s -> %s"},
    {"%s -> %s (saiu de %s)", "%s -> %s (removed from %s)"},
    {"%s -> %s (%s ficou com %s)", "%s -> %s (%s now uses %s)"},
    {"%s é a única tecla de %s", "%s is the only key for %s"},
    {"%s precisa de uma tecla", "%s needs a key"},
    {"Padrões restaurados", "Defaults restored"},
    {"Estilo dos menus",
     "Menu style"},
    {"Moderno",
     "Modern"},
    {"Original",
     "Original"},
    {"MENU DE PAUSA",
     "PAUSE MENU"},
    {"OPÇÕES",
     "OPTIONS"},
    {"OPÇÕES /",
     "OPTIONS /"},
    {"VÍDEO",
     "VIDEO"},
    {"GRÁFICOS",
     "GRAPHICS"},
    {"INTERFACE",
     "INTERFACE"},
    {"ÁUDIO",
     "AUDIO"},
    {"CONTROLES",
     "CONTROLS"},
    {"TECLAS",
     "KEY BINDINGS"},
    {"CONTROLE",
     "CONTROLLER"},
    {"TELA",
     "DISPLAY"},
    {"VISÃO",
     "VIEW"},
    {"QUALIDADE DA IMAGEM",
     "IMAGE QUALITY"},
    {"TEXTURAS",
     "TEXTURES"},
    {"RECONSTRUÇÃO",
     "UPSCALING"},
    {"HUD",
     "HUD"},
    {"MENUS",
     "MENUS"},
    {"VOLUME",
     "VOLUME"},
    {"ENTRADA",
     "INPUT"},
    {"OLHAR",
     "LOOK"},
    {"MOUSE",
     "MOUSE"},
    {"TECLAS",
     "KEY BINDINGS"},
    {"ANALÓGICOS",
     "STICKS"},
    {"BOTÕES",
     "BUTTONS"},
    {"Campo de visão",
     "Field of view"},
    {"Aparelho em uso",
     "Active device"},
    {"Esquema de botões",
     "Button layout"},
    {"Sensibilidade do mouse",
     "Mouse sensitivity"},
    {"Sensibilidade mirando",
     "Aiming sensitivity"},
    {"Sensibilidade do analógico",
     "Look sensitivity"},
    {"Zona morta",
     "Dead zone"},
    {"Atirar com os gatilhos",
     "Fire with triggers"},
    {"Aplicar mudanças",
     "Apply changes"},
    {"AÇÃO",
     "ACTION"},
    {"PRINCIPAL",
     "PRIMARY"},
    {"SECUNDÁRIA",
     "SECONDARY"},
    {"CONTROLE",
     "CONTROLLER"},
    {"Restaurar as teclas padrão",
     "Restore default bindings"},
    {"Valor atual:",
     "Current value:"},
    {"Há mudanças não aplicadas: aplique com %s.",
     "Unapplied changes: apply them with %s."},
    {"Resolução da cena 3D",
     "3D render resolution"},
    {"Restaurar padrões",
     "Restore to defaults"},
    {"Escolher",
     "Select"},
    {"Voltar",
     "Back"},
    {"Aplicar",
     "Apply"},
    {"Mudar",
     "Change"},
    {"Trocar",
     "Rebind"},
    {"Limpar",
     "Clear"},
    {"Cancelar",
     "Cancel"},
    {"Ligar/desligar",
     "Toggle"},
    {"Ajustar",
     "Adjust"},
    {"Manter esta configuração?",
     "Keep these settings?"},
    {"A configuração de vídeo anterior volta em %d s.",
     "The previous video settings return in %d s."},
    {"Manter",
     "Keep changes"},
    {"Desfazer",
     "Revert"},
    {"Restaurar os padrões?",
     "Restore defaults?"},
    {"As opções de %s voltam aos valores padrão.",
     "Every setting in %s goes back to its default."},
    {"Restaurar",
     "Restore"},
    {"Cancelar",
     "Cancel"},
    {"Configuração aplicada",
     "Settings applied"},
    {"Padrões restaurados",
     "Defaults restored"},
    {"Configuração de vídeo mantida",
     "Video settings kept"},
    {"A configuração anterior voltou",
     "Previous video settings restored"},
    {"O estilo novo vale na próxima vez que as opções abrirem",
     "The new menu style shows the next time the options open"},
    {"Aperte uma tecla ou botão do mouse para %s",
     "Press a key or mouse button for %s"},
    {"Aperte um botão do controle para %s",
     "Press a controller button for %s"},
    {"Essa coluna é do teclado e do mouse",
     "That column takes a key or mouse button"},
    {"Essa coluna é do controle",
     "That column takes a controller button"},
    {"Esc cancela, Delete limpa",
     "Esc cancels, Delete clears"},
    {"—",
     "—"},
    {"Nightfire PC",
     "Nightfire PC"},
    {"007: Nightfire · SLUS-20579",
     "007: Nightfire · SLUS-20579"},
    {"Versão do port: %s",
     "Port build: %s"},
    {"PS2 > PC",
     "PS2 > PC"},
    {"O tamanho da janela do jogo, ou o modo do monitor na tela cheia exclusiva. Uma resolução nova precisa ser confirmada em 15 segundos; senão, a anterior volta.",
     "The size of the game window, or the monitor mode in exclusive fullscreen. A new resolution must be confirmed within 15 seconds or the previous one returns."},
    {"Janela com bordas, janela sem bordas do tamanho do monitor, ou tela cheia exclusiva, que troca o modo do monitor.",
     "Windowed, borderless (a window the size of the monitor) or exclusive fullscreen, which switches the monitor's mode."},
    {"Espera o monitor antes de mostrar cada quadro: sem cortes na imagem, com um pouco mais de atraso na resposta.",
     "Waits for the monitor's refresh before showing each frame: no tearing, at the cost of a little input latency."},
    {"O campo de visão vertical, em graus. Telas mais largas veem mais dos lados. O PS2 usa 60.",
     "The vertical field of view in degrees. Wider screens see more to the sides. The PS2 game uses 60."},
    {"Desenha a cena 3D em outra resolução e a leva ao tamanho da janela. Acima de 100% é superamostragem: bordas mais limpas, mais trabalho da placa de vídeo. O HUD e os menus ficam na resolução da janela.",
     "Renders the 3D scene at another resolution and scales it to the window. Above 100% is supersampling: cleaner edges for more GPU work. The HUD and menus stay at window resolution."},
    {"Anti-aliasing por multiamostragem: suaviza as bordas da cena 3D. A névoa é calculada em cada amostra.",
     "Multisample anti-aliasing smooths the edges of the 3D scene. The fog is computed per sample."},
    {"Como as texturas são lidas de longe e de lado. O anisotrópico mantém pisos e paredes nítidos.",
     "How textures are sampled at a distance and at an angle. Anisotropic keeps floors and walls sharp."},
    {"Usa as texturas em alta resolução da pasta textures (um pacote do PCSX2) onde houver.",
     "Uses the high-resolution replacements from the textures folder (a PCSX2 texture pack) where they exist."},
    {"DLSS (super resolução) e DLAA (anti-aliasing) da NVIDIA. Chegam numa versão futura.",
     "NVIDIA DLSS super resolution and DLAA anti-aliasing. Coming in a later version."},
    {"Redesenha o HUD, as legendas e os menus originais na resolução da janela, com formas e fontes vetoriais. A disposição continua a do jogo.",
     "Redraws the original HUD, subtitles and menus at window resolution with vector shapes and fonts. The layout stays the game's."},
    {"Em telas largas, leva os painéis do HUD às bordas da janela em vez da área 4:3 do meio. Precisa da interface HD.",
     "On wide screens, moves the HUD panels to the edges of the window instead of the centred 4:3 area. Needs the HD interface."},
    {"Moderno desenha os menus do PC com este visual. Original mantém os menus do PS2 como eram. Vale na próxima vez que as opções abrirem.",
     "Modern draws the PC menus with this design. Original keeps the PS2 menus as they were. Takes effect the next time the options open."},
    {"O idioma dos menus e textos só do PC. O texto do jogo continua em inglês.",
     "The language of the PC-only menus and texts. The game's own text stays in English."},
    {"Os volumes do jogo.",
     "The game's volume levels."},
    {"O aparelho que mexeu no jogador por último. Os símbolos dos comandos acompanham.",
     "The device that last moved the player. The button prompts follow it."},
    {"Os oito esquemas de botões do jogo do PS2 para o controle.",
     "The PS2 game's eight button layouts for the controller."},
    {"Mouse para a frente olha para baixo.",
     "Moving the mouse forward looks down."},
    {"O analógico direito para a frente olha para baixo.",
     "Pushing the right stick forward looks down."},
    {"Quanto a visão gira para cada movimento do mouse. O valor é a sensibilidade x 1000 (radianos por contagem do "
     "sensor); o medidor mostra quantos centímetros o mouse anda para uma volta completa.",
     "How far the view turns per mouse movement. The value is the sensitivity x 1000 (radians per sensor count); the "
     "meter shows how far the mouse travels for one full turn."},
    {"A velocidade do mouse mirando, multiplicada por este valor (e dividida pelo zoom).",
     "Mouse speed while aiming, multiplied by this value (and divided by the zoom)."},
    {"Multiplica o analógico direito antes das curvas de aceleração do próprio jogo.",
     "Multiplies the right stick before the game's own acceleration curves."},
    {"Quanto um analógico precisa andar para contar. O PS2 usa 31%.",
     "How far a stick must move before it counts. The PS2 uses 31%."},
    {"Ligado: RT e LT fazem R1 e L1 (atirar e mirar no esquema padrão). Desligado: os botões de ombro fazem.",
     "On: RT and LT work as R1 and L1 (fire and aim in the default layout). Off: the bumpers do."},
    {"A vibração dos tiros, das explosões e das armas.",
     "Rumble from hits, explosions and weapons."},
    {"Aplica as mudanças de Vídeo, Gráficos e Interface. Sair das opções sem aplicar as descarta.",
     "Applies the changes made in Video, Graphics and Interface. Leaving the options without applying discards them."},
    {"Escolha uma célula e aperte a tecla, o botão do mouse ou o botão do controle novo. Esc cancela, Delete limpa. Entre parênteses, o que o esquema de botões do jogo faz.",
     "Select a cell and press the new key, mouse button or controller button. Esc cancels, Delete clears. In brackets: what the game's button layout does."},
    {"Volta todas as teclas e botões aos padrões.",
     "Puts every key and button back to the defaults."},
    {"JOGABILIDADE", "GAMEPLAY"},
    {"MIRA", "AIMING"},
    {"MOVIMENTO", "MOVEMENT"},
    {"ITENS E HUD", "ITEMS & HUD"},
    {"TEXTO NA TELA", "ON-SCREEN TEXT"},
    {"Mira automática", "Auto aim"},
    {"Mira na tela", "Crosshairs"},
    {"Mira manual", "Manual aim"},
    {"Agachar", "Crouch"},
    {"Troca automática de arma", "Weapon auto switch"},
    {"Itens piscando", "Flashing objects"},
    {"HUD sempre visível", "HUD always on"},
    {"Legendas", "Subtitles"},
    {"Alternar", "Toggle"},
    {"Segurar", "Hold"},
    {"Bond mira sozinho nos inimigos perto do centro da tela.",
     "Bond automatically aims at enemies near the center of the screen."},
    {"Mirar com precisão nos inimigos fica por sua conta.", "You are responsible for precisely aiming at enemies."},
    {"Uma pequena mira mostra para onde você está apontando.",
     "A small crosshair indicates what you're currently aiming at."},
    {"A mira só aparece enquanto você mira manualmente.", "Crosshair only appears while you're manually aiming."},
    {"Apertar o botão de mira manual liga e desliga a mira manual.",
     "Pressing the Manual Aim button toggles manual aim on and off."},
    {"É preciso segurar o botão de mira manual para mirar manualmente.",
     "You must hold down the Manual Aim button in order to manually aim."},
    {"Apertar o botão de agachar agacha e levanta.", "Pressing the Crouch button turns crouch on and off."},
    {"É preciso segurar o botão de agachar para ficar agachado.",
     "You must hold down the Crouch button in order to crouch."},
    {"Ao pegar uma arma melhor que a da mão, você passa a usá-la na hora.",
     "If you pick up a weapon with a better ranking, you automatically switch to that weapon."},
    {"Ao pegar uma arma melhor que a da mão, você continua com a atual.",
     "If you pick up a weapon with a better ranking, you don't change your current weapon."},
    {"Itens piscando: os itens que dá para pegar pulsam quando você chega perto.",
     "Flashing objects on: items you can pick up pulse when you get close to them."},
    {"Itens sem piscar: de perto, os itens ficam como são.",
     "Flashing objects off: items keep their normal look up close."},
    {"O HUD fica sempre na tela.", "HUD is always displayed."},
    {"O HUD some quando você não está levando dano.", "HUD is hidden when you're not taking damage."},
    {"As legendas de todas as falas aparecem.", "Subtitles for all dialogue are displayed."},
    {"As legendas não aparecem, a não ser em algumas falas escolhidas.",
     "Subtitles are not displayed except for specially selected lines."},
    {"O volume da música do jogo.", "Volume of the in game music."},
    {"O volume dos efeitos sonoros do jogo.", "Volume of the in game sfx."},
    {"Sinta a ação: o controle vibra com os tiros, as explosões e as armas.",
     "Feel the action: the controller rumbles with hits, explosions and weapons."},
    {"Sem sentir a ação: o controle não vibra.", "Don't feel the action: the controller does not rumble."},
    {"O volume muda na hora e fica guardado no perfil e no nightfire.ini.",
     "The volume changes at once and is kept in your profile and in nightfire.ini."},
    {"PAUSA /", "PAUSE /"},
    {"Continuar", "Resume"},
    {"Volta à missão", "Back to the mission"},
    {"Recomeçar a missão", "Restart mission"},
    {"Do primeiro nível dela", "From its first level"},
    {"Sair da missão", "Quit mission"},
    {"Volta ao menu principal", "Back to the main menu"},
    {"Opções", "Options"},
    {"Vídeo, controles e teclas", "Video, controls and keys"},
    {"A missão começa de novo do primeiro nível, com os objetivos dela.", "The mission starts over from its first level, objectives and all."},
    {"Você volta ao menu principal e o progresso nesta missão se perde.", "You return to the main menu and progress in this mission is lost."},
    {"Nível %d de %d", "Level %d of %d"},
    {"Dificuldade:", "Difficulty:"},
    {"Objetivos cumpridos", "Objectives complete"},
    {"Objetivo confidencial", "Classified objective"},
    {"Confidencial", "Classified"},
    {"Revelado quando a missão chegar a ele.", "Revealed as the mission unfolds."},
    {"Situação:", "Status:"},
    {"Cumpridos", "Complete"},
    {"Fracassados", "Failed"},
    {"Confidenciais", "Classified"},
    {"Objetivos da missão", "Mission objectives"},
    {"Esquema", "Layout"},
    {"Mapa de botões", "Button map"},
    {"As teclas e os botões se trocam em Opções › Teclas.", "Keys and buttons can be rebound in Options › Key bindings."},
    {"Continuar", "Resume"},
    {"Nenhum objetivo visível nesta fase.", "No objectives are visible in this level."},
    {"Sem dica para este objetivo.", "No hint for this objective."},
    {"Aparelho", "Device"},
    {"Objetivos", "Objectives"},
    {"Controles", "Controls"},
    {"Placar", "Score"},
    {"Recomeça esta fase com o equipamento que você tinha.", "Restart this level with the gear you had."},
    {"Recomeça a missão do primeiro nível.", "Restart the mission from its first level."},
    {"Volta ao menu principal.", "Return to the main menu."},
    {"Continuar", "Continue"},
    {"Para a próxima missão", "On to the next mission"},
    {"Estatísticas", "Stats"},
    {"A missão em números", "The mission in numbers"},
    {"Dossiê", "Dossier"},
    {"Nenhuma medalha registrada", "No medal recorded"},
    {"Medalha", "Medal"},
    {"Bônus", "Bonus"},
    {"Volte ao resultado para abrir o dossiê.", "Go back to the results to open the dossier."},
    {"O multijogador ainda não foi portado.", "Multiplayer is not available yet."},
    {"As missões de carro ainda não foram portadas: esta foi pulada e a seguinte foi liberada.",
     "Driving missions are not available yet: this one was skipped and the next one unlocked."},
    {"Menu principal", "Main menu"},
    {"Sair do jogo", "Quit game"},
    {"Volta para a área de trabalho", "Back to the desktop"},
    {"Sair do Nightfire?", "Quit Nightfire?"},
    {"O progresso que não foi gravado num codinome se perde.", "Progress that was not saved to a codename will be lost."},
    {"Sair", "Quit"},
    {"Vídeo, gráficos, interface, áudio e controles da versão para PC.", "Video, graphics, interface, audio and controls of the PC version."},
    {"Fecha o jogo e volta para a área de trabalho.", "Close the game and return to the desktop."},
    {"Campanha de um jogador", "Single player campaign"},
    {"Ainda não disponível", "Not available yet"},
    {"Perfis, opções e códigos", "Profiles, options and codes"},
    {"Codinome", "Codename"},
    {"Dificuldade", "Difficulty"},
    {"Missão", "Mission"},
    {"Criar", "Create"},
    {"Sem gravar", "Not saved"},
    {"Perfil padrão", "Default profile"},
    {"Gravado", "Saved"},
    {"Lendo os dados gravados…", "Reading save data…"},
    {"Bloqueada", "Locked"},
    {"De carro", "Driving"},
    {"As missões de carro ainda não foram portadas: escolher esta a pula e libera a seguinte.", "Driving missions are not available yet: choosing this one skips it and unlocks the next."},
    {"Esta missão ainda está bloqueada.", "This mission is still locked."},
    {"Melhor medalha:", "Best medal:"},
    {"Nenhuma", "None"},
    {"Recorde:", "Best score:"},
    {"Missões", "Missions"},
    {"Ainda não disponível", "Not available yet"},
    {"Ainda não disponível nesta versão.", "Not available in this version yet."},
    {"Até 8 letras, algarismos e espaços. Digite no teclado ou escolha as teclas da tela.", "Up to 8 letters, digits and spaces. Type on the keyboard or pick the keys on screen."},
    {"Digite um código secreto e escolha Pronto.", "Type a secret code and choose Done."},
    {"Espaço", "Space"},
    {"Apagar", "Delete"},
    {"Pronto", "Done"},
    {"Dados gravados", "Save data"},
    {"Aviso", "Notice"},
    {"Confirmar", "Confirm"},
    {"Controle desconectado", "Controller disconnected"},
    {"Conecte o controle de novo, ou aperte uma tecla para seguir com o teclado e o mouse.", "Reconnect the controller, or press a key to carry on with the keyboard and mouse."},
    {"Codinome:", "Codename:"},
    {"Código:", "Code:"},
    {"Aperte START", "Press START"},
    {"Controles depur.", "Debug controls"},
    {"Voo livre", "Fly mode"},
    {"Todas as armas", "All weapons"},
    {"Invencível", "Invincible"},
    {"HUD", "HUD"},
    {"Coordenadas", "Coordinates"},
    {"Tela larga", "Widescreen"},
    {"Personagens", "Characters"},
    {"IA cega", "Blind characters"},
    {"IA: depuração", "AI debug"},
    {"Jogador falso", "Dummy player"},
    {"Pode congelar", "Allow freeze"},
    {"Música", "Music"},
    {"Todas as salas", "Draw all rooms"},
    {"Gravidade zero", "Zero gravity"},
    {"Travamento", "Lockup check"},
    {"Brilho", "Brightness"},
    {"Liberar tudo", "Unlock all"},
    {"Fim da missão", "End mission"},
    {"Fracassar a missão", "Fail mission"},
    {"Cumprir a missão", "Complete mission"},
    {"Cumprir: bronze", "Complete: bronze"},
    {"Cumprir: prata", "Complete: silver"},
    {"Cumprir: ouro", "Complete: gold"},
    {"Cumprir: platina", "Complete: platinum"},
    {"Recordes, recompensas, aparelhos e armas", "Records, rewards, gadgets and weapons"},
    {"Créditos", "Credits"},
    {"Quem fez o jogo", "The people behind the game"},
    {"Os créditos do próprio jogo, no ritmo dele; voltar sai antes do fim.", "The game's own credits, at its own pace; back leaves before the end."},
    {"Dossiê", "Dossier"},
    {"Requer:", "Requires:"},
    {"Recompensas", "Rewards"},
    {"Melhoria", "Upgrade"},
    {"Nível de melhoria:", "Upgrade level:"},
    {"Nenhuma medalha ainda", "No medal yet"},
    {"Rolar", "Scroll"},
    {"As estatísticas da missão até agora, ao lado das metas com que a pontuação é medida.", "The mission's statistics so far, next to the targets the score is measured against."},
    {"Limite de quadros", "Frame rate limit"},
    {"%d fps", "%d fps"},
    {"Interpolação de quadros", "Frame interpolation"},
    {"Mouse sem atraso", "Instant mouse look"},
    {"Movimento", "Motion"},
    {"O máximo de quadros por segundo. O jogo continua a 60 Hz; com a interpolação, cada quadro a mais mostra o movimento "
     "entre dois ticks. Desligado: sem limite (com a sincronia vertical, o ritmo do monitor).",
     "The most frames per second the game draws. The game itself still runs at 60 Hz; with interpolation, each extra "
     "frame shows the motion between two ticks. Off: no limit (with VSync, the monitor's refresh rate)."},
    {"Desenha entre os ticks de 60 Hz do jogo: a câmera e os personagens andam suaves em monitores acima de 60 Hz, com "
     "um tick (16 ms) de atraso. Desligada: cada quadro mostra o último tick, como antes.",
     "Draws between the game's 60 Hz ticks: the camera and the characters move smoothly on monitors above 60 Hz, one "
     "tick (16 ms) behind. Off: every frame shows the latest tick, as before."},
    {"Com a interpolação, o mouse gira a visão na hora, sem o tick de atraso (só em primeira pessoa, fora das cenas e dos "
     "menus). O jogo recebe o mesmo movimento no tick seguinte.",
     "With interpolation, the mouse turns the view at once, without the tick of delay (first person only, outside scenes "
     "and menus). The game gets the same motion on its next tick."},
    {"Adaptador de vídeo", "Graphics adapter"},
    {"Adaptador (reiniciar)", "Adapter (restart)"},
    {"Automático", "Automatic"},
    {"Integrado", "Integrated"},
    {"Dedicado", "Discrete"},
    {"A placa de vídeo que desenha o jogo. Automático: a padrão do Windows (a \"Preferência de gráficos\" do jogo nas "
     "configurações). Integrado: a do processador, que gasta menos energia; Dedicado: a placa de maior desempenho. Vale "
     "na próxima vez que o jogo abrir. Com o monitor ligado a outra placa, cada quadro é copiado entre as duas. Em uso: %s.",
     "The graphics card that draws the game. Automatic: the Windows default (the game's \"Graphics preference\" in "
     "Windows settings). Integrated: the processor's, which uses less power; Discrete: the most powerful card. Takes "
     "effect the next time the game starts. With the monitor on the other card, every frame is copied between the two. "
     "In use: %s."},
    {"Reinicie o jogo para usar o novo adaptador de vídeo", "Restart the game to use the new graphics adapter"},
    {"Volta completa (360°)", "Full turn (360°)"},
    {"%.1f cm a 800 DPI  ·  %.1f cm a 1600 DPI", "%.1f cm at 800 DPI  ·  %.1f cm at 1600 DPI"},
    {"Estilo de mira", "Aim style"},
    {"Automático", "Automatic"},
    {"Moderno", "Modern"},
    {"Clássico", "Classic"},
    {"Moderno: segurar o botão de mirar leva a arma ao olho, com a alça e a massa no centro da tela, um zoom leve e a "
     "mira presa no centro; as armas de luneta e de zoom entram na luneta do jogo (a roda dá o zoom). Clássico: a mira "
     "do jogo, que se move pela tela com a arma no quadril. Automático: moderno com o teclado e o mouse, clássico com o "
     "controle.",
     "Modern: holding aim raises the gun to your eye, with the sights on the centre of the screen, a light zoom and the "
     "aim fixed at the centre; scoped and zoom weapons go into the game's scope (the wheel zooms). Classic: the game's "
     "own aim, a crosshair that moves on the screen with the gun at the hip. Automatic: modern with keyboard and mouse, "
     "classic with a controller."},
    {"Câmera lenta da roda de armas", "Weapon wheel slow motion"},
    {"Pausa", "Pause"},
    {"%d%% da velocidade", "%d%% speed"},
    {"Desligada", "Off"},
    {"Com a roda de armas aberta (segurar Tab, ou L3 no controle) o jogo anda mais devagar, para escolher com calma; o "
     "som continua no tempo real. Desligada: o jogo segue na velocidade normal; Pausa: o jogo para.",
     "While the weapon wheel is open (hold Tab, or L3 on a controller) the game slows down so you can choose calmly; "
     "sound keeps real time. Off: the game keeps its normal speed; Pause: the game stops."},
    {"Roda de armas", "Weapon wheel"},
    {"Armas", "Weapons"},
    {"Aparelhos", "Gadgets"},
    {"Soltar para equipar", "Release to equip"},
    {"Soltar para continuar", "Release to keep"},
    {"Sem munição", "No ammo"},
    {"Na mão", "In hand"},
    {"Desarmado", "Unarmed"},
    {"Configuração", "Setup"},
    {"007 Nightfire", "007 Nightfire"},
    {"Escolha a imagem do seu disco de 007: Nightfire (PS2, EUA)",
     "Select your 007: Nightfire disc image (PS2, USA)"},
    {"O port joga com a sua própria cópia do jogo: a imagem do disco (.iso) ou o disco num leitor de DVD. Nada é baixado, e o disco não é alterado.",
     "The port plays your own copy of the game: a disc image (.iso) or the disc in a DVD drive. Nothing is downloaded, and your disc is not changed."},
    {"Escolher a imagem", "Select disc image"},
    {"Arquivo .iso", ".iso file"},
    {"Usar o disco da unidade %s", "Use the disc in drive %s"},
    {"Leitor de DVD", "DVD drive"},
    {"Jogar", "Play"},
    {"Começar o jogo", "Start the game"},
    {"Fazer uma cópia em arquivo único", "Make a single-file copy"},
    {"Sair", "Quit"},
    {"Imagem do disco", "Disc image"},
    {"Abre a escolha de arquivo. Escolha a imagem .iso do seu disco de 007: Nightfire para PlayStation 2, versão americana (SLUS-20579). A escolha fica guardada no nightfire.ini.",
     "Opens a file picker. Choose the .iso image of your 007: Nightfire disc for the PlayStation 2, US version (SLUS-20579). Your choice is saved in nightfire.ini."},
    {"Leitor de DVD", "DVD drive"},
    {"Lê o jogo direto do disco nesse leitor. Deixe o disco no leitor enquanto joga.",
     "Reads the game straight from the disc in that drive. Keep the disc in the drive while you play."},
    {"Jogar", "Play"},
    {"Começa o jogo com este disco. Da próxima vez ele abre direto, sem esta tela.",
     "Starts the game with this disc. Next time it starts directly, without this screen."},
    {"Escolha um disco primeiro.", "Select a disc first."},
    {"Cópia em arquivo único", "Single-file copy"},
    {"Grava um só .exe com o seu jogo dentro, para jogar sem a imagem do disco. Uns %s.",
     "Writes one .exe with your game inside, so you can play without the disc image. About %s."},
    {"Este arquivo contém o seu jogo: é só para você. Não o compartilhe.",
     "This file contains your game: it is for you only. Do not share it."},
    {"Sair", "Quit"},
    {"Fecha o port.", "Closes the port."},
    {"Disco:", "Disc:"},
    {"Salvar como:", "Save as:"},
    {"Nenhum disco escolhido ainda.", "No disc selected yet."},
    {"Pronto: 007: Nightfire (PS2, EUA), %s", "Ready: 007: Nightfire (PS2, USA), %s"},
    {"Pronto: os arquivos do jogo (versão não conferida)", "Ready: the game files (version not checked)"},
    {"O arquivo não foi encontrado.", "The file was not found."},
    {"Não deu para ler isto como imagem de disco (ISO 9660).",
     "This could not be read as a disc image (ISO 9660)."},
    {"Isto não é um disco de PlayStation 2.", "This is not a PlayStation 2 disc."},
    {"Este disco é de outro jogo (%s).", "This disc is a different game (%s)."},
    {"Esta é outra versão de 007: Nightfire (%s). Por enquanto só a americana (SLUS-20579) funciona.",
     "This is another version of 007: Nightfire (%s). Only the US version (SLUS-20579) is supported for now."},
    {"Falta um arquivo do jogo: %s.", "A game file is missing: %s."},
    {"Este disco não serve", "This disc can't be used"},
    {"OK", "OK"},
    {"Cancelar", "Cancel"},
    {"Escolha o arquivo na janela que abriu.", "Choose the file in the window that opened."},
    {"Escolha a imagem do seu disco de 007: Nightfire (PS2, EUA)",
     "Select your 007: Nightfire disc image (PS2, USA)"},
    {"Imagens de disco (*.iso)", "Disc images (*.iso)"},
    {"Todos os arquivos (*.*)", "All files (*.*)"},
    {"Fazendo a cópia", "Making the copy"},
    {"Copiando o seu jogo… %d%%", "Copying your game… %d%%"},
    {"Cópia pronta", "Copy made"},
    {"Gravada em:\n%s", "Saved as:\n%s"},
    {"A cópia não deu certo", "The copy failed"},
    {"A pasta está dentro de um repositório git: a cópia não é gravada ali.",
     "That folder is inside a git repository: the copy is not written there."},
    {"A cópia teria o nome do próprio executável.", "The copy would overwrite the running program."},
    {"O disco não tem SYSTEM.CNF: a versão não dá para conferir.",
     "The disc has no SYSTEM.CNF: its version cannot be checked."},
    {"Falta no disco: %s", "Missing from the disc: %s"},
    {"Falta espaço no disco de destino (precisa de %s).",
     "Not enough free space on the target drive (it needs %s)."},
    {"Não deu para gravar %s.", "Could not write %s."},
    {"Não deu para ler %s.", "Could not read %s."},
    {"A cópia foi cancelada.", "The copy was cancelled."},
    {"Imagem do disco", "Disc image"},
    {"Pasta", "Folder"},
    {"Leitor de DVD", "DVD drive"},
    {"Cópia em arquivo único", "Single-file copy"},
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == size_t(PcText::Count), "uma linha por PcText");

std::string g_language = "en";
bool g_hd_text = false;
uint32_t g_generation = 1;
char g_buffers[size_t(PcText::Count)][192];

// Letra sem acento de cada caractere de 0xc0 a 0xff (Windows-1252 = Latin-1 nessa faixa).
const char kFold[65] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

bool cp1252_defined(uint8_t c) {
    // De 0x80 a 0x9f, os buracos do Windows-1252.
    return !(c == 0x81 || c == 0x8d || c == 0x8f || c == 0x90 || c == 0x9d || c == 0x7f);
}

bool font_has_glyph(uint8_t c) {
    if (c < 0x80) return c >= 0x20 && c != 0x7f;
    if (g_hd_text) return cp1252_defined(c);
    const FONT_tag* f = FontTable[1].font;  // a fonte 2 (SerpLight), a dos menus
    if (f == nullptr || f->chars == nullptr || f->count == 0 || f->count > 0x200) return false;
    for (uint32_t i = 0; i < f->count; ++i) {
        if (f->chars[i].code == c) return true;
    }
    return false;
}

// Um ponto de código Unicode em Windows-1252 (0: não existe).
uint8_t to_cp1252(uint32_t cp) {
    if (cp < 0x80 || (0xa0 <= cp && cp <= 0xff)) return uint8_t(cp);
    switch (cp) {
        case 0x2026: return 0x85;
        case 0x0152: return 0x8c;
        case 0x2018: return 0x91;
        case 0x2019: return 0x92;
        case 0x201c: return 0x93;
        case 0x201d: return 0x94;
        case 0x2013: return 0x96;
        case 0x2014: return 0x97;
        case 0x2122: return 0x99;
        default: return 0;
    }
}

}  // namespace

void PcText_SetLanguage(const std::string& lang) {
    const std::string l = lang == "en" ? "en" : "pt";
    if (l == g_language) return;
    g_language = l;
    ++g_generation;
}

const std::string& PcText_Language() { return g_language; }

void PcText_SetHdText(bool on) {
    if (on == g_hd_text) return;
    g_hd_text = on;
    ++g_generation;
}

bool PcText_HdText() { return g_hd_text; }

uint32_t PcText_Generation() { return g_generation; }

const char* PcText_Utf8(PcText id, const std::string& lang) {
    const size_t i = size_t(id);
    if (i >= size_t(PcText::Count)) return "";
    return lang == "en" ? kTable[i].en : kTable[i].pt;
}

const char* PcText_Get(PcText id) {
    const size_t i = size_t(id);
    if (i >= size_t(PcText::Count)) return "";
    const std::string s = PcText_ToGame(PcText_Utf8(id, g_language));
    std::snprintf(g_buffers[i], sizeof(g_buffers[i]), "%s", s.c_str());
    return g_buffers[i];
}

std::string PcText_ToGame(const char* utf8, bool (*has_glyph)(uint8_t)) {
    if (has_glyph == nullptr) has_glyph = font_has_glyph;
    std::string out;
    const auto* p = reinterpret_cast<const uint8_t*>(utf8);
    while (p != nullptr && *p != 0) {
        uint32_t cp = *p++;
        int extra = 0;
        if (cp >= 0xf0) {
            cp &= 0x07;
            extra = 3;
        } else if (cp >= 0xe0) {
            cp &= 0x0f;
            extra = 2;
        } else if (cp >= 0xc0) {
            cp &= 0x1f;
            extra = 1;
        } else if (cp >= 0x80) {
            cp = 0xfffd;  // byte de continuação solto
        }
        for (; extra > 0 && (*p & 0xc0) == 0x80; --extra) cp = cp << 6 | (*p++ & 0x3f);
        if (extra > 0) cp = 0xfffd;
        if (cp == '\n') {
            out.push_back('\n');
            continue;
        }
        uint8_t c = to_cp1252(cp);
        if (c != 0 && has_glyph(c)) {
            out.push_back(char(c));
            continue;
        }
        // Sem glifo: a letra sem acento, se ela tiver glifo.
        if (c >= 0xc0 && has_glyph(uint8_t(kFold[c - 0xc0]))) {
            out.push_back(kFold[c - 0xc0]);
        } else if (c == 0x85) {
            out += "...";
        } else if (c == 0x91 || c == 0x92) {
            out.push_back('\'');
        } else if (c == 0x93 || c == 0x94) {
            out.push_back('"');
        } else if (c == 0x96 || c == 0x97) {
            out.push_back('-');
        } else if (c == 0xa0) {
            out.push_back(' ');
        } else {
            out.push_back('?');
        }
    }
    return out;
}

// ---- Textos do jogo trocados no PC ----

namespace {

// Os rótulos do USATxt.dat que falam do hardware do PS2 e o texto do PC de cada um (pc_text.h; a lista também em
// docs/port.md, "Textos do PC no lugar dos do PS2"). O jogo usa as mensagens do cartão como formato do snprintf com o
// slot ("1") e, em algumas, os KB livres (0x52): "%.0s" consome o slot sem imprimir.
const PcLabelOverride kLabelOverrides[] = {
    // A caixa do cartão (PS2_DoCardFlow, a mensagem de PS2CardFlow; o grupo inteiro de mensagens do cartão).
    {0x267, "memory card", "Não foi possível gravar os dados.%.0s São necessários %d KB livres no disco para gravar o progresso.",
     "Save data cannot be written.%.0s At least %d KB of free disk space is needed to save your progress."},
    {0x268, "memory card", "Espaço insuficiente no disco.%.0s Libere pelo menos %d KB para gravar o progresso.",
     "Not enough free disk space.%.0s Free at least %d KB to save your progress."},
    {0x269, "memory card", "A pasta dos dados gravados não está pronta. Prepará-la agora?",
     "The save folder is not set up. Set it up now?"},
    {0x26a, "memory card", "Espaço insuficiente no disco.%.0s O NightFire precisa de %d KB livres para gravar.",
     "Not enough free disk space.%.0s NightFire needs %d KB free to save data."},
    {0x26b, "memory card", "A pasta dos dados gravados não foi encontrada.", "The save folder could not be found."},
    {0x26c, "memory card", "Gravando os dados. Não feche o jogo.", "Saving data. Please do not close the game."},
    {0x26d, "memory card", "Falha ao gravar! Confira a pasta dos dados gravados e tente de novo.",
     "Save failed! Check the save folder and please try again."},
    {0x26e, "memory card", "Falha ao gravar! Erro na pasta dos dados gravados.", "Save failed! The save folder could not be written."},
    {0x26f, "memory card", "Gravando automaticamente. Não feche o jogo.", "Autosaving data. Please do not close the game."},
    {0x271, "memory card", "Nenhum dado gravado do NightFire foi encontrado.", "No NightFire save data found."},
    {0x272, "memory card", "A pasta dos dados gravados não está pronta.", "The save folder is not set up."},
    {0x273, "memory card", "Falha ao carregar! Confira a pasta dos dados gravados e tente de novo.",
     "Load failed! Check the save folder and please try again."},
    {0x274, "memory card", "Falha ao carregar! Erro nos dados gravados.", "Load failed! The save data could not be read."},
    {0x275, "memory card", "Carregando os dados. Não feche o jogo.", "Loading data. Please do not close the game."},
    {0x277, "memory card", "Conferindo os dados gravados…", "Checking save data…"},
    {0x278, "memory card", "Erro nos dados gravados! Eles podem estar corrompidos.", "Save data error! The data may be corrupt."},
    {0x279, "memory card", "Apagar todos os dados gravados da pasta?", "Erase all save data in the save folder?"},
    {0x27a, "memory card", "Preparando a pasta dos dados gravados. Não feche o jogo.",
     "Setting up the save folder. Please do not close the game."},
    {0x27c, "memory card", "Não foi possível preparar a pasta dos dados gravados. Confira a pasta e tente de novo.",
     "The save folder could not be set up. Please check it and try again."},
    {0x3e6, "memory card", "Apagando os dados. Não feche o jogo.", "Deleting data. Please do not close the game."},
    {0x3e7, "memory card", "Falha ao apagar! Confira a pasta dos dados gravados e tente de novo.",
     "Delete failed! Check the save folder and please try again."},
    // O controle e o multitap (MenuManager_Monitor: a caixa da página 0x4000002f, com o número do controle).
    {0x3c0, "DUALSHOCK", "Conecte o controle %s", "Connect controller %s"},
    {0x3c9, "DUALSHOCK", "O controle %s está desconectado.", "Controller %s is disconnected."},
    {0x10000ba, "multitap", "Conecte um controle para cada jogador.", "Connect a controller for each player."},
    {0x10001df, "multitap", "Conecte os controles de novo e aperte START para continuar.",
     "Please reconnect the controllers and press START to continue."},
};
constexpr size_t kLabelOverrideCount = sizeof(kLabelOverrides) / sizeof(kLabelOverrides[0]);
char g_label_buffers[kLabelOverrideCount][256];

// As conversões de um formato do printf, na ordem: 's', 'd' (também %i e '*' de largura ou precisão), ou a letra.
std::string conversions(const char* f) {
    std::string out;
    for (const char* p = f; p != nullptr && *p != 0; ++p) {
        if (*p != '%') continue;
        ++p;
        if (*p == '%') continue;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') ++p;
        for (; (*p >= '0' && *p <= '9') || *p == '*' || *p == '.'; ++p) {
            if (*p == '*') out.push_back('d');
        }
        while (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'z' || *p == 'j' || *p == 't' || *p == 'I') ++p;
        if (*p == 0) break;
        out.push_back(*p == 'i' ? 'd' : *p);
    }
    return out;
}

bool contains_nocase(const char* text, const char* part) {
    const size_t n = std::strlen(part);
    for (const char* t = text; *t != 0; ++t) {
        size_t k = 0;
        while (k < n && t[k] != 0 && std::tolower(uint8_t(t[k])) == std::tolower(uint8_t(part[k]))) ++k;
        if (k == n) return true;
    }
    return n == 0;
}

}  // namespace

size_t PcText_LabelOverrideCount() { return kLabelOverrideCount; }

const PcLabelOverride& PcText_LabelOverrideAt(size_t i) { return kLabelOverrides[i < kLabelOverrideCount ? i : 0]; }

bool PcText_FormatPrefix(const char* pc, const char* game) {
    const std::string a = conversions(pc), b = conversions(game);
    return a.size() <= b.size() && b.compare(0, a.size(), a) == 0;
}

const char* PcText_GameLabel(uint32_t label, const char* game) {
    for (size_t i = 0; i < kLabelOverrideCount; ++i) {
        const PcLabelOverride& o = kLabelOverrides[i];
        if (o.label != label) continue;
        if (game == nullptr || !contains_nocase(game, o.match)) return nullptr;
        const char* utf8 = g_language == "en" ? o.en : o.pt;
        if (!PcText_FormatPrefix(utf8, game)) return nullptr;
        const std::string s = PcText_ToGame(utf8);
        std::snprintf(g_label_buffers[i], sizeof(g_label_buffers[i]), "%s", s.c_str());
        return g_label_buffers[i];
    }
    return nullptr;
}
