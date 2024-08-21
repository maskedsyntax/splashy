from logging import root
from tkinter import *
from tkinter.colorchooser import askcolor
from tkinter import ttk
import tkinter as tk


def paint(event, selected_color):
	x1, y1 = event.x - 1, event.y - 1
	x2, y2 = event.x + 1, event.y + 1
	canvas.create_line(x1, y1, x2, y2, fill=selected_color, width=5)


def change_color(selected_color):
	global current_color
	current_color = selected_color


def clear(canvas):
	canvas.delete('all')


foreground_color = 'white'
current_color = 'black'

root = tk.Tk()
root.title("Splashyyy")
root.geometry("1050x570+150+50")
root.configure(bg="#f2f3f5")
root.resizable(True, True)

# TODO: create and insert logo here
# image_icon = PhotoImage(file="")


#buttons
red_button = tk.Button(text='red', foreground='white', background='red', relief='flat', command=lambda: change_color('red'))
blue_button = tk.Button(text='blue', foreground='white', background='blue', relief='flat', command=lambda: change_color('blue'))
green_button = tk.Button(text='green', foreground='white', background='green', relief='flat', command=lambda: change_color('green'))
black_button = tk.Button(text='black', foreground='white', background='black', relief='flat', command=lambda: change_color('black'))
clear_button = tk.Button(text='Clear', foreground='black', background='#c4c4c4', relief='flat', command=lambda: clear(canvas))


# Place buttons horizontally above drawing area
red_button.pack(side=LEFT)
blue_button.pack(side=LEFT)
green_button.pack(side=LEFT)
black_button.pack(side=LEFT)
clear_button.pack(side=LEFT)


canvas = tk.Canvas(root, borderwidth=5, highlightthickness=0, background="#ffffff")
# canvas.grid(column=0, row=1, columnspan=5)
canvas.pack(fill=BOTH, expand=True)
canvas.bind("<B1-Motion>", lambda event: paint(event, current_color))


root.mainloop()
