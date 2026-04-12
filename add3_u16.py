import numpy as np


def add3_u16(img1, img2, img3):
    a = img1.astype(np.uint16)
    b = img2.astype(np.uint16)
    c = img3.astype(np.uint16)
    return a + b + c
