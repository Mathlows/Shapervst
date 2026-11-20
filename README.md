# ShaperVST

Plugin d'automation de volume (type *volume shaper* / *sidechain*), au format **VST3 / AU**, utilisable dans
Ableton Live (toutes éditions), FL Studio, Logic, Bitwig, Reaper, Cubase…

> Nom de travail : à remplacer avant toute distribution (une seule ligne à modifier dans `CMakeLists.txt`).

## Fonctionnalités

| | |
|---|---|
| **Éditeur de courbe façon LFO Serum** | points, tension par segment, sauts verticaux (steps), grille, peinture de steps |
| **Multibande** | 3 bandes Linkwitz-Riley 24 dB/oct, somme à plat (vérifiée par les tests), une courbe + profondeur par bande |
| **Modes de déclenchement** | *Host sync* (calé sur la position du morceau), *MIDI retrigger* (une note relance la courbe), *MIDI envelope* (une note joue la courbe une fois) — MIDI à l'échantillon près |
| **Vitesse** | divisions 8 mesures → 1/64, pointé / triolet, ou libre en Hz |
| **Smooth** | lissage 0–50 ms du gain : supprime les clics sur les sauts brusques |
| **Divers** | phase, mix (dry aligné en phase en mode multibande), gain de sortie, affichage du signal d'entrée derrière la courbe, tête de lecture |

### Contrôles de l'éditeur de courbe

| Action | Geste |
|---|---|
| Ajouter un point | double-clic dans le vide |
| Supprimer un point | double-clic sur le point |
| Déplacer un point | glisser (X aimanté à la grille) |
| Courber un segment | glisser la poignée ronde au milieu du segment (vers le haut = courbe vers le haut) |
| Remettre un segment droit | double-clic sur sa poignée |
| Peindre des steps | **Alt** + glisser |
| Réglage fin | **Shift** + glisser |
| Ignorer la grille | **Ctrl / Cmd** + glisser |
| Formes prédéfinies, inverser, retourner, copier/coller, copier vers toutes les bandes | clic droit |

Les premier et dernier points sont fixés en X (comme dans Serum).

## Déclenchement MIDI dans Ableton Live

1. Mettre ShaperVST sur la piste audio, mode **MIDI retrigger** ou **MIDI envelope**.
2. Créer une piste MIDI (ex. le kick en MIDI, ou un clip dédié).
3. Dans *MIDI To* de cette piste, choisir la piste audio puis **ShaperVST**.

## Compilation

Prérequis : CMake ≥ 3.22, un compilateur C++20 (Xcode sur macOS, Visual Studio 2022 sur Windows). JUCE est téléchargé
automatiquement.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target ShaperVST_VST3     # + ShaperVST_AU sur macOS
ctest --test-dir build -C Release                                 # tests
```

Le plugin se trouve dans `build/ShaperVST_artefacts/Release/VST3/ShaperVST.vst3`, à copier dans :

- macOS : `~/Library/Audio/Plug-Ins/VST3/` (et `.component` dans `~/Library/Audio/Plug-Ins/Components/`)
- Windows : `C:\Program Files\Common Files\VST3\`

Sans rien installer : chaque push sur GitHub lance la CI (onglet **Actions**) qui produit les plugins macOS (Universal),
Windows et Linux en artefacts téléchargeables.

> macOS : un plugin téléchargé non signé est bloqué par Gatekeeper. Pour vos propres builds :
> `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ShaperVST.vst3`.
> Une distribution publique demande une signature + notarisation Apple (compte développeur).

## Architecture

```
Source/
  dsp/            cœur DSP sans dépendance JUCE (testable, portable)
    ShapeCurve    modèle de courbe : points, tensions, steps, presets, sérialisation
    Crossover     crossover 3 bandes LR4 (filtres SVF TPT, stables en modulation)
    PhaseEngine   phase : sync hôte / retrigger / enveloppe
  PluginProcessor traitement audio, paramètres (automatisables), état, MIDI
  PluginEditor    fenêtre principale
  ui/CurveEditor  éditeur de courbe façon Serum
tests/
  DspTests        tests unitaires du cœur DSP
  HostTest        « hôte » hors-ligne : pilote le vrai plugin (tempo, MIDI, état, multibande) + capture de l'UI
```

Temps réel : le thread audio n'alloue pas et ne bloque jamais. Les courbes sont rendues en tables (2049 points) sur le
thread UI puis récupérées par le thread audio via un `try-lock` non bloquant.

## Licence JUCE

JUCE 8 est sous double licence : **AGPLv3** (code source ouvert obligatoire) ou **licence commerciale** (gratuite
« Starter » sous un certain seuil de revenus). À choisir avant de distribuer.

## Pistes pour la suite

- Annuler / rétablir dans l'éditeur, sélection multiple de points
- Déclenchement par transitoires audio (sidechain audio)
- Courbes dans un mode « dB » plutôt que linéaire, presets utilisateur sur disque
- Format CLAP (via `clap-juce-extensions`), signature et installeurs
