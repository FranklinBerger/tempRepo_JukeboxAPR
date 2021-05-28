import numpy as np
import math
import textwrap
import os.path as op
from PIL import Image, ImageDraw, ImageFont
import io
import pprint
import paho.mqtt.client as paho
import time

broker="192.168.5.1"
port=1883
client1= paho.Client("control1")
client1.connect(broker,port)


FILE_NAME = 'soda.jpg'
CHILD_FOLDER = '/img/'
FOLDER_PATH = op.normpath(op.dirname(op.abspath(__file__)) + CHILD_FOLDER)
PATH2 = op.join(FOLDER_PATH, FILE_NAME)

# Declaring variables for maths
PI = math.pi
LEDS = 55
BANDS = 360
RADIAN = PI / 180
OFFSET = 5
SIZE = 2*LEDS
DEPTH = 3

def to_polar(img):
    img.show()
    byte_str = b""
    img_array = np.asarray(img)
    polar = np.zeros((BANDS, LEDS - OFFSET, DEPTH), dtype=np.uint8)
    for band in reversed(range(BANDS)):
        alpha = band * RADIAN
        for pixel in range(OFFSET, LEDS):
            x = LEDS + int(pixel * math.cos(alpha))
            y = LEDS - int(pixel * math.sin(alpha))
            color = img_array[y][x]
            polar[band, pixel - OFFSET] = color
            for num in np.nditer(color):
                byte_str += bytes([num])
    img = Image.fromarray(polar)
    img.show()
    return byte_str

#Recreates an image from its "polar" equivalent
def from_polar(byte_str):
    mv = memoryview(byte_str)
    array = np.zeros((SIZE, SIZE, DEPTH), dtype=np.uint8)
    pixels = LEDS - OFFSET
    for band in range(BANDS):
        alpha = band * RADIAN
        for pixel in range(0, pixels):
            first = DEPTH * (pixels * band + pixel)
            third = first + 3
            colour = list(mv[first:third])
            x = LEDS + int((pixel + OFFSET) * math.cos(alpha))
            y = LEDS - int((pixel + OFFSET) * math.sin(alpha))
            array[y,x] = colour
            img = Image.fromarray(array)
    return img



def crop_resize(img):
    width, height = img.size
    offset = int(abs(height-width)/2)
    if width < height:
        left = 0
        top = offset
        right = width
        bottom = height - offset
        cropped_img = img.crop((left, top, right, bottom))
        resized = cropped_img.resize((SIZE, SIZE))
    elif height < width:
        left = offset
        top = 0
        right = width - offset
        bottom = height
        cropped_img = img.crop((left, top, right, bottom))
        resized = cropped_img.resize((SIZE, SIZE))
    else:
        resized = img.resize((SIZE, SIZE))
    return resized

def to_exponential (array):
    array = list(array)
    for i in range(len(array)):
        array[i] = int((array[i] * array[i]) / 256)
    return bytes(array)

"""
one = []
size = 50
for i in range(360):
    if i < 90:
        one.extend(b"\x00\x00\x00"*size)
    elif i < 180:
        one.extend(b"\xff\x00\x00"*size)
    elif i < 270:
        one.extend(b"\x00\xff\x00"*size)
    elif i < 360:
        one.extend(b"\x00\x00\xff"*size)
one = bytearray(one)

img = from_polar(one)
#img.show()

img = Image.open(PATH)
img = img.convert('RGB')
#polar = to_polar(img)
#img = from_polar(polar)


polar = b""
for num in np.nditer(img):
    polar += bytes([num])
#print(one == polar)

#img = from_polar(polar)
# img.show()
"""

img2 = Image.open(PATH2)
img2 = img2.convert('RGB')
img2 = crop_resize(img2)
array = to_polar(img2)
array = to_exponential(array)

#print(array)

img2 = from_polar(array)
img2.show()

topic='test'

odd=0
while True:
    print("Publish")

    client1.publish(topic, b"start_transmitting")
    #time.sleep(5)
    #img2 = Image.open(PATH2)
    #img2 = img2.convert('RGB')
    #img2 = crop_resize(img2)
    #array = to_polar(img2)

    #for i in range(0,len(array), 150):
    client1.publish(topic,array)
    time.sleep(10)



def build_from_meta(author, title, size):
    FONT_SIZE = int(size / 18)
    CENTER_RADIUS = int(size/22)
    TOP_LEFT = int(size / 2) - CENTER_RADIUS
    BOTTOM_RIGHT = int(size / 2) + CENTER_RADIUS
    BOX_HEIGHT = int(math.sqrt(math.pow((size / 2), 2) / 2))
    TEXT_MAX_WIDTH = 2 * BOX_HEIGHT
    PAD = int(size / 50)
    PATH = op.normpath(op.dirname(op.abspath(__file__)))
    FONT_FOLDER = 'fonts/'
    FONT_NAME = 'unispace.ttf'
    FONT_PATH = op.normpath(op.join(PATH,FONT_FOLDER,FONT_NAME))
    BACKGROUND_COLOR = 'red'
    DISK_COLOR = 'black'

    #Variables
    start_height = int(size/2) - BOX_HEIGHT

    #Taille de la police
    img = Image.new("RGB", (size, size), color=BACKGROUND_COLOR)
    draw = ImageDraw.Draw(img)

    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    #Taille de pixels par caractères
    char_width = draw.textlength("A", font=font)

    char_nb = int(TEXT_MAX_WIDTH / char_width)
    #Maximum de ligne pour la variable title = 3
    lines = textwrap.wrap(title, width=char_nb, max_lines=3, )
    #Dessin des deux cercles dans le carré
    draw = ImageDraw.Draw(img)
    draw.ellipse((0, 0, size, size), fill=DISK_COLOR)
    draw.ellipse((TOP_LEFT, TOP_LEFT, BOTTOM_RIGHT, BOTTOM_RIGHT), fill=BACKGROUND_COLOR)
    #Écriture du texte de la variable "title" sur le cercle
    for line in lines:
        w, h = draw.textsize(line, font=font)
        draw.text(((size - w) / 2, start_height), line, font=font)
        start_height += h + PAD
    #Maximun de ligne pour la variable author = 3
    lines = textwrap.wrap(author, width=char_nb, max_lines=3)
    start_height = 4*size/7
    #Écriture du texte de la variable "author" sur le cercle
    for line in lines:
        w, h = draw.textsize(line, font=font)
        draw.text(((size - w) / 2, start_height), line, font=font)
        start_height += h + PAD

    return img
