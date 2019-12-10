# othello
Jeu de l'othello en C via les sockets

## Compilation

### Linux

Compiler le programme avec make:

`>make`

Lancer le programme:

`>./othello <port>`

### Mac

1. Installer XQuartz
2. Installer socat
3. Compiler le programme avec `./docker/build.sh`
4. Lancer le programme avec `./docker/run.sh <port>`

## Tâches:

1. Passer les deux clients en écoute sur leur port argument.
2. L'un des deux clients se connecte à l'autre et ferme son port d'écoute.

TODO: https://github.com/CNAM-AVA/othello/issues/1
