'''
This script is meant to convert a picture into a picture that can be loaded and used by the VGA driver.
Its steps consist of:
1. Load the image using PIL
2. Convert the image into 16 colors using the quantize method of PIL
3. Resize the image to fit the VGA screen (640x480)
4. Convert the image into a 2D array of pixels, where each pixel is represented by a 16-bit color value
5. Save the image as a C header file that can be included in the VGA driver code
'''
import os
import sys
from PIL import Image
import numpy as np
import argparse

def closest_color(pixel, palette):
    """Find the closest color in the palette to the given pixel."""
    r, g, b = pixel
    closest_index = min(
        enumerate(palette),
        key=lambda color: (r - color[1][0])**2 + (g - color[1][1])**2 + (b - color[1][2])**2
    )[0]
    # print(closest_index)
    return closest_index

def convert_image_to_vga(image_path, output_path):
    # Load the image using PIL
    img = Image.open(image_path)

    # # Convert the image into 16 colors using the quantize method of PIL
    # img = img.convert('RGB').convert('P', palette=Image.ADAPTIVE, colors=16)
    # Create a palette for 16 colors with 16 specific colors
    # Define the custom palette (16 colors in RGB format)
    palette = [
        (0, 0, 0), (46, 210, 62), (35, 168, 59), (66, 245, 75),
        (0, 37, 233), (3, 192, 237), (1, 128, 239), (29, 251, 248),
        (236, 41, 19), (213, 207, 66), (217, 140, 49), (205, 247, 79),
        (191, 53, 240), (185, 195, 234), (183, 131, 239), (255, 255, 255)
    ]

    # Convert the image to RGB mode to get the pixel values
    img = img.convert('RGB')

    # Resize the image to fit the VGA screen (640x480)
    img = img.resize((640, 480))

    # Convert the image into a 2D array of pixels
    pixels = np.array(img)

    # Map each pixel to the closest color in the palette
    # make a 2d array of pixels with the same shape as the resized image
    new_pixels = np.zeros((pixels.shape[0], pixels.shape[1]), dtype=np.uint16)
    print(new_pixels.shape)
    for i in range(pixels.shape[0]):
        for j in range(pixels.shape[1]):
            new_pixels[i, j] = closest_color(pixels[i, j], palette)

    # Convert the new pixel array back to an image
    # img = Image.fromarray(np.uint8(new_pixels))


    # Create the output directory if it doesn't exist
    # and check if the output path has a directory
    if os.path.dirname(output_path):
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        

    # Save the image as a C header file
    with open(output_path, 'w') as f:
        f.write('#ifndef VGA_IMAGE_H\n')
        f.write('#define VGA_IMAGE_H\n\n')
        f.write('const unsigned short vga_image[480 * 640] = {\n')
        for row in new_pixels:
            f.write(', '.join(map(str, row)) + ',\n')
        f.write('};\n\n')
        f.write('#endif // VGA_IMAGE_H\n')

    print(f'Image converted and saved to {output_path}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert an image to a VGA-compatible format.')
    parser.add_argument('image_path', type=str, help='Path to the input image file.')
    parser.add_argument('output_path', type=str, help='Path to the output C header file.')
    args = parser.parse_args()

    convert_image_to_vga(args.image_path, args.output_path)
    # Example usage:
    # convert_image_to_vga('input_image.png', 'output_image.h')
    # Terminal command:
    # python picture.py input_image.png output_image.h

