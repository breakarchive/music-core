🎵 MusicCore

MusicCore est un lecteur audio Windows simple et moderne, développé en C++ avec Dear ImGui, DirectX 11 et miniaudio.

L'objectif est de proposer un lecteur léger, personnalisable et agréable à utiliser, avec une interface moderne et un fond d'écran personnalisable.

 Version 1.0 — Projet en développement

Cette version constitue une première base du projet. Beaucoup de fonctionnalités sont prévues pour les prochaines versions.

 Fonctionnalités actuelles
 Lecture de fichiers MP3, WAV et FLAC
 Ajout de plusieurs musiques
 Import de musiques depuis un dossier
 Glisser-déposer de fichiers audio
 Morceau précédent
 Lecture / Pause
 Stop
 Morceau suivant
 Contrôle du volume
 Égaliseur 6 bandes
60 Hz
150 Hz
400 Hz
1 kHz
2.4 kHz
8 kHz
 Fond d'écran personnalisable
 Flou du fond réglable
 Support des GIFs animés en arrière-plan
 Sauvegarde automatique de la playlist et des paramètres
 Barre de progression avec possibilité de changer la position dans la musique
 Interface personnalisée avec Dear ImGui
 Accélération graphique avec DirectX 11
 Technologies utilisées
C++
Dear ImGui
DirectX 11
miniaudio
stb_image
Win32 API
🚀 Installation / Compilation

Le projet est actuellement prévu pour Windows.

Pour compiler le projet, il faut notamment disposer de :

Un compilateur C++ compatible Windows
Windows SDK
DirectX 11
Les fichiers sources de Dear ImGui
miniaudio.h
stb_image.h

Les bibliothèques nécessaires sont déjà indiquées dans le code avec les directives #pragma comment(lib, ...).

Une fois compilé, lance simplement l'exécutable MusicCore.exe.

 Configuration

MusicCore crée un fichier :

musiccore_config.txt


Ce fichier permet de conserver notamment :

La playlist
Le fond d'écran choisi
Le niveau de flou du fond

Les paramètres sont sauvegardés automatiquement lorsque l'application est fermée ou lorsque certains réglages sont modifiés.

 Roadmap

MusicCore est actuellement en V1. Le projet va continuer à évoluer.

 Prévu pour les prochaines versions
 Discord Rich Presence / RPC
 Amélioration de l'interface
 Améliorations de l'égaliseur
 Visualiseur audio
 Mode aléatoire
 Répétition des morceaux
 Affichage de davantage d'informations sur le morceau
 Plus de paramètres de personnalisation
 Amélioration de la gestion des fonds et animations
 Optimisations des performances
 Et plein d'autres fonctionnalités
 Version

Current version: V1

MusicCore est encore un projet en développement. Cette V1 sert principalement de base pour les futures fonctionnalités.

🇬🇧 MusicCore

MusicCore is a simple and modern Windows audio player built with C++, Dear ImGui, DirectX 11 and miniaudio.

The goal is to create a lightweight, customizable and enjoyable music player with a modern interface and customizable backgrounds.

 Version 1.0 — Work in progress

This is the first version of the project and serves as a foundation for many future features.

 Current Features
 Support for MP3, WAV and FLAC
 Add multiple music files
 Import music from a folder
 Drag and drop audio files
 Previous track
 Play / Pause
 Stop
 Next track
 Volume control
 6-band equalizer
60 Hz
150 Hz
400 Hz
1 kHz
2.4 kHz
8 kHz
 Custom background
 Adjustable background blur
 Animated GIF background support
 Automatic playlist and settings saving
 Music progress bar with seeking
 Custom Dear ImGui interface
 DirectX 11 hardware acceleration
 Technologies
C++
Dear ImGui
DirectX 11
miniaudio
stb_image
Win32 API
 Building

The project is currently designed for Windows.

You will need:

A Windows-compatible C++ compiler
Windows SDK
DirectX 11
Dear ImGui source files
miniaudio.h
stb_image.h

The required libraries are already specified in the source code using #pragma comment(lib, ...).

After compiling the project, simply launch MusicCore.exe.

 Configuration

MusicCore creates a file called:

musiccore_config.txt


This file stores information such as:

Your playlist
Selected background
Background blur level

Settings are automatically saved when the application closes or when certain settings are changed.

 Roadmap

MusicCore is currently in V1, but development will continue.

 Planned Features
 Discord Rich Presence / RPC
 Improved UI
 Better equalizer features
 Audio visualizer
 Shuffle mode
 Repeat mode
 More detailed track information
 More customization options
 Improved background and animation support
 Performance optimizations
 And many more features
 Version

Current version: V1

MusicCore is still under development. This V1 is mainly the foundation for future updates and new features.
