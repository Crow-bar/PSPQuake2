# **PSP Quake2**

Порт Quake 2 (Id Software, Inc.) для PlayStation Portable.

> Основная цель добиться полной совместимости со всеми моделями PSP.

## Выполнено:

* Файловая система c прямым обращением к функциям sceIo
* Менеджер памяти из Quake 1
* Карта коллизий загрузка с динамическим распределением памяти
* Рендеринг, на выбор при сборке: аппаратный, программный
* Звук, на выбор при сборке: audio, vaudio
* Сетевая игра Infrastructure и Adhoc
* MP3 проигрыватель
* CTF игра


## Не выполнено:

* Поддержка загрузки входных параметров через файл (start.cmd)
* Бокс с информацией об ошибке
* Спящий режим
* MIP-текстурирование в аппаратном рендеринге
* Разделяемые игровые модули


## Проблемы:

* Недостаточно памяти для работы всех карт одиночной игры на PSP-1000
> Решаемо
* Пропадают полигоны при аппаратном рендеринге
> Вероятно ошибка в коде программного отсечения
* Сбой при попытке открыть меню Multiplayer
> Сбой происходит исключительно при отсутствии сохраненых точек доступа Wi-Fi


## Сборка:

### Параметры(первое значение по умолчанию):

	BUILD      (debug, release) - Режим сборки
	REF              (soft, gu) - Рендеринг: soft - программный, gu - аппаратный
	GAME            (base, ctf) - Игра
	USE_VAUDIO           (0, 1) - Вывод звука через vaudio
	USE_CDMP3            (0, 1) - MP3 проигрыватель
	USE_STDIN            (0, 1) - Ввод команд через tty режим PSPLink

### Пример:

#### make:

	make -j8 BUILD=release REF=gu USE_CDMP3=1

> Для сборки с параметром `USE_VAUDIO=1`, требуется библиотека [libpspvaudio](https://github.com/Crow-bar/libpspvaudio)

#### clean:

	make clean-all

#### make & install:

	make install -j8 INSTALL_DIR=dist BUILD=release REF=gu USE_CDMP3=1


## Установка:

1) Скопировать файл `EBOOT.PBP` в `ms0:/PSP/GAME/Quake2`
2) Скопировать папку `baseq2` из оригинальной игры для PC в `ms0:/PSP/GAME/Quake2`
3) Для работы mp3 проигрывателя необходимо скопировать треки в папку `ms0:/PSP/GAME/Quake2/baseq2/music`
   треки должны иметь название `Track01.mp3, Track02.mp3 .. Track99.mp3`  от 1 до 99.



#

# **PSP Quake2(EN)**

Port of Quake 2 (Id Software, Inc.) for the PlayStation Portable.

> The main goal is to achieve full compatibility with all PSP models.

## Done:

* File system with direct access to sceIo functions
* Memory manager from Quake 1
* Сollision map loading with dynamic memory allocation
* Rendering, to choose from when assembling: hardware, software
* Sound, to choose from when assembling: audio, vaudio
* Network game Infrastructure and Adhoc
* MP3 player
* CTF game


## Not done:

* Support for loading input parameters via file (start.cmd)
* Box with error information
* Sleep mode
* MIP-texturing in hardware rendering
* Shareable game modules


## Problems:

* Not enough memory to run all single player maps on PSP-1000
> Solvable
* Disappearing polygons on hardware rendering
> Probably a bug in the software clipping code
* Crash when trying to open Multiplayer menu
> Failure occurs only if there are no saved Wi-Fi hotspots


## Assembly:

### Parameters (first default):

	BUILD      (debug, release) - Build mode
	REF              (soft, gu) - Rendering: soft - software, gu - hardware
	GAME            (base, ctf) - Game
	USE_VAUDIO           (0, 1) - Audio output via vaudio
	USE_CDMP3            (0, 1) - MP3 player
	USE_STDIN            (0, 1) - Entering commands via tty mode PSPLink

### Example:

#### make:

	make -j8 BUILD=release REF=gu USE_CDMP3=1

> To build with `USE_VAUDIO=1`, the [libpspvaudio](https://github.com/Crow-bar/libpspvaudio) library is required

#### clean:

	make clean-all

#### make & install:

	make install INSTALL_DIR=dist BUILD=release REF=gu USE_CDMP3=1


## Installation:

1) Copy the file `EBOOT.PBP` to `ms0:/PSP/GAME/Quake2`
2) Copy the `baseq2` folder from the original PC game to `ms0:/PSP/GAME/Quake2`
3) For the mp3 player to work, you need to copy the tracks to the folder `ms0:/PSP/GAME/Quake2/baseq2/music`
    tracks should be named `Track01.mp3, Track02.mp3 .. Track99.mp3` from 1 to 99.
