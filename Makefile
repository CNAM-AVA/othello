all:
	rm -f othello
	gcc -Wall -o bin/othello_GUI src/othello_GUI.c $$(pkg-config --cflags --libs gtk+-3.0)
	ln -s bin/othello_GUI othello

clean:
	rm bin/othello_GUI
	rm othello
