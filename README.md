# **PSP Quake2**

Порт Quake 2 (Id Software, Inc.) для PlayStation Portable.

Полная совместимость со всеми моделями PSP.

## Выполнено:

* Загрузка входных параметров(argv) из файла start.cmd
* Менеджер памяти из Quake 1
* Рендеринг: аппаратный, программный
* Звук: audio, vaudio
* Сетевая игра: Infrastructure, Adhoc
* Проигрывание MP3
* Разделяемые игровые модули

## Текущие задачи:

* MIP-текстурирование в аппаратном рендеринге

## Сборка:

### Параметры(первое значение по умолчанию):

	BUILD(debug, release, prof) - Режим сборки
	REF              (soft, gu) - Рендеринг: soft - программный, gu - аппаратный
	GAME            (base, ctf) - Игра
	USE_VAUDIO           (0, 1) - Вывод звука через vaudio
	USE_CDMP3            (0, 1) - MP3 проигрыватель
	USE_STDIN            (0, 1) - Ввод команд через tty режим PSPLink

### Пример:

#### Сборка:

	make -j8 BUILD=release REF=gu USE_CDMP3=1

> Для сборки с параметром `USE_VAUDIO=1`, требуется библиотека [libpspvaudio](https://github.com/Crow-bar/libpspvaudio)

#### Очистка:

	make clean-all

#### Сборка и очистка:

	make install -j8 INSTALL_DIR=dist BUILD=release REF=gu USE_CDMP3=1


## Установка:

1) Скопировать файл `EBOOT.PBP` в `ms0:/PSP/GAME/Quake2`
2) Скопировать папку `baseq2` из оригинальной игры для PC в `ms0:/PSP/GAME/Quake2`
3) Для работы mp3 проигрывателя необходимо скопировать треки в папку `ms0:/PSP/GAME/Quake2/baseq2/music`
   треки должны иметь название `Track01.mp3, Track02.mp3 .. Track99.mp3` от 1 до 99.


#


# **PSP Quake2(EN)**

A port of Quake 2 (Id Software, Inc.) for PlayStation Portable.

Fully compatible with all PSP models.

## Completed:

* Loading input parameters(argv) from the start.cmd file
* Memory Manager from Quake 1
* Rendering: hardware, software
* Sound: audio, vaudio
* Network game: Infrastructure, Adhoc
* MP3 playback
* Shared game modules

## Current tasks:

* MIP texturing in hardware rendering

## Assembly:

### Parameters(the first default value):
	BUILD(debug, release, prof) - Build mode
	REF              (soft, gu) - Rendering: soft - software, gu - hardware
	GAME            (base, ctf) - Game
	USE_VAUDIO           (0, 1) - Audio output via vaudio
	USE_CDMP3            (0, 1) - MP3 player
	USE_STDIN            (0, 1) - Entering commands via PSPLink tty mode

### Example:

#### make:
	make -j8 BUILD=release REF=gu USE_CDMP3=1
> To build with the `USE_VAUDIO=1` parameter, the [libpspvaudio] library is required(https://github.com/Crow-bar/libpspvaudio )

#### clean:

	make clean-all

#### make & install:

	make install -j8 INSTALL_DIR=dist BUILD=release REF=gu USE_CDMP3=1

## Installation:

1) Copy the file "EBOOT.PBP" to the folder `ms0:/PSP/GAME/Quake2`
2) Copy the "baseq2" folder from the original PC game to `ms0:/PSP/GAME/Quake2`.
3) To play music, you need to copy the tracks to the following folder: `ms0:/PSP/GAME/Quake2/baseq2/music`.
The files should be named Track01.mp3, Track02.mp3 ... Track99.mp3 (from 1 to 99).

#
