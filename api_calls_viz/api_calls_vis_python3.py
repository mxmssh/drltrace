import os
import sys
import random
import pylab
import numpy
import argparse
from PIL import Image
import math
from colorsys import hsv_to_rgb


# A user can add his/her own list of API calls here
marked_calls = ["GetProcAddress"]

all_dots = list()

css_styles = ""
divs = ""
divs_image = ""


def get_specific_color(api_name):
    if api_name not in marked_calls:
        return 0
    print("Picked hardcoded color for ", api_name)
    return 0xFF0000


def add_one_color_in_legend(color, libcall_name):
    global css_styles, divs
    str_color = str(hex(color))
    str_color = str_color.replace("0x", "")
    css_color = ".name%s { background: #%x;}\n" % (str_color, color)
    div_color = "%s<div class=\"foo name%s\"></div><br/>\n" % (
        libcall_name, str_color)
    css_styles += css_color
    divs += div_color


def add_one_color_on_html_page(color, libcall_name):
    global divs_image
    str_color = str(hex(color))
    str_color = str_color.replace("0x", "")
    div_image_color = "<div class=\"foo name%s\" title = \"%s\"></div>\n" % (
        str_color, libcall_name)
    divs_image += div_image_color


def choose_colors(unique_libcalls, grayscale):
    libcall_colors_dict = dict()

    print("Generating random color for each entry")
    # generate a random unique color for each
    # call excluding whiteness (checkit)
    dot_nums = random.sample(range(0x0, 0x00EEFFFF), len(unique_libcalls))
    print("Done")

    for i, api_name in enumerate(unique_libcalls):
        # we assign specific red color for marked calls
        color_dot = get_specific_color(api_name)
        # FIXIT: we disable marking in case of grayscale
        # due to strange img results
        if color_dot != 0 and not grayscale:
            add_one_color_in_legend(color_dot, api_name)
        else:
            color_dot = dot_nums[i]
            add_one_color_in_legend(dot_nums[i], api_name)

        libcall_colors_dict[api_name] = color_dot
    return libcall_colors_dict


def add_dots_on_image(
        libcalls_colors_dict, libcalls_seq, html_page_name, grayscale):
    libcalls_count = len(libcalls_seq)
    print("Adding %d colors on image/images" % libcalls_count)
    for api_name in libcalls_seq:
        # take an RGB color associated with API call
        color_dot = libcalls_colors_dict.get(api_name, None)
        if color_dot is None:
            print("[CRITICAL] Failed to find color for %s, exiting" % api_name)
            sys.exit(0)
        if html_page_name is not None:
            add_one_color_on_html_page(color_dot, api_name)
        R = (color_dot & 0x00FF0000) >> 16
        G = (color_dot & 0x0000FF00) >> 8
        B = color_dot & 0x000000FF
        if grayscale is True:
            color_gray = (R + G + B)/3
            color_dot = (color_gray, color_gray, color_gray)
        else:
            color_dot = (R, G, B)
        all_dots.append(color_dot)
    return all_dots


def create_image(all_dots, image_name):
    # calculate width and height
    total_size = math.sqrt(len(all_dots))
    w = int(total_size) + 1
    h = int(total_size) + 1

    print("Generating picture %dx%d" % (h, w))

    data = numpy.zeros((h, w, 3), dtype=numpy.uint8)
    i = 0
    j = 0
    for dot in all_dots:
        data[i][j] = dot
        j += 1
        if j >= w:
            i += 1
            j = 0
    img = Image.fromarray(data, 'RGB')
    img.save(image_name)
    print("Done")


def create_html_page(name):
    global css_styles, divs_image
    # page headers */
    print("Generating HTML image")

    html_image = "<html><head><title>%s</title>\n\
    <style>\n\
    .foo {\
      display: inline-block;\
      width: 20px;\
      height: 5px;\
      margin: 1px;\
      border: 1px solid rgba(0, 0, 0, .2);\
    }\n" % name

    for css_style in css_styles:
        html_image += css_style
    html_image += "\n</style>\n<body>\n"
    for div in divs_image:
        html_image += div
    html_image += "</body></head></html>"
    with open(name, 'w') as file:
        file.write(html_image)
    print("Done")


def create_html_legend(name):
    global css_styles, divs
    # page headers */
    print("Generating legend in HTML")
    html_legenda = "<html><head><title>Legenda</title>\n\
    <style>\n\
    .foo {\
      display: inline-block;\
      width: 20px;\
      height: 5px;\
      margin: 3px;\
      border: 1px solid rgba(0, 0, 0, .2);\
    }\n"

    for css_style in css_styles:
        html_legenda += css_style
    html_legenda += "\n</style>\n<body>\n"
    for div in divs:
        html_legenda += div
    html_legenda += "</body></head></html>"

    with open("legend_%s.html" % name, 'w') as file:
        file.write(html_legenda)
    print("Done")


def gen_image(trace_name, image_name, html_page_name, grayscale):
    unique_libcalls = list()
    libcalls_seq = list()

    # to be able to generate the same image each run for single trace
    random.seed(0)

    with open(file=trace_name, mode="r", encoding="utf8") as file:
        content = file.readlines()
        # TODO: what if I randomly selected red 0x255 0x0 0x0?
        for line in content:
            # skip arguments provided by drltrace
            if "    arg" in line or "module id" in line:
                continue
            # extract API call name
            line = line[:line.find("(")]
            api_name = line[line.find("!")+1:]

            if api_name not in unique_libcalls:
                unique_libcalls.append(api_name)  # save in unique calls
            # save in sequence for future proceedings
            libcalls_seq.append(api_name)

        entries_count = len(libcalls_seq)

        if entries_count <= 0:
            print("Failed to find any API calls matching dll_name!"
                  + "api_name pattern in the file specified")
            sys.exit(0)
        else:
            print("Found %d api calls in the file" % entries_count)

        print("Starting image generation")

        # choose colors for each unique image
        libcall_colors_dict = choose_colors(unique_libcalls, grayscale)
        dots = add_dots_on_image(
            libcall_colors_dict, libcalls_seq, html_page_name, grayscale)
        create_image(dots, image_name)
        create_html_legend(image_name)
        if html_page_name is not None:
            create_html_page(html_page_name)


def main():
    # bug: whites for many calls ?
    parser = argparse.ArgumentParser(prog="api_calls_vis.py",
                                     description='A script for API calls trace'
                                     + 'visualization.',
                                     usage='% (prog)s - t calc.exe.txt - i'
                                     + 'calc.jpeg - ht calc.html')
    parser.add_argument('-t', '--trace', required=True,
                        help="A trace file with API calls in the following"
                        + "format \"library_name!api_call_name\"")
    parser.add_argument('-i', '--image', default="tmp.jpeg",
                        help="A name of image file")  # generate jpeg image
    # generate html page with image
    parser.add_argument('-ht', '--html', help="A name of html page (heavy)")
    parser.add_argument('-gr', '--grayscale', dest='grayscale',
                        action='store_true',
                        help="Generate an image in grayscale")
    args = parser.parse_args()
    gen_image(args.trace, args.image, args.html, args.grayscale)


if __name__ == "__main__":
    main()
