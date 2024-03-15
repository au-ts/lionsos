import pygame
import threading
import time
import sys

pygame.init()

clock = pygame.time.Clock()

# Set up display
screen = pygame.display.set_mode((0, 0), pygame.FULLSCREEN)
pygame.display.set_caption('Kitty server')

def scale_image(image, scalar):
    w, h = image.get_size()
    scaled_image = pygame.transform.scale(image, (int(scalar * w), int(scalar * h)))
    return scaled_image

# Load snack images
food_dict = dict()
food_dict["1234"] = (
    scale_image(pygame.image.load('mars_bar.png'), 0.3),
    "mars bar"
)
food_dict["5678"] = (
    scale_image(pygame.image.load('potato_chips.png'), 0.15),
    "packet of chips"
)

lock = threading.Lock()

current_tap = None
current_tap_time = 0

def current_time():
    return int(time.time() * 1000)

def register_tap(card_uid):
    global current_tap
    global current_tap_time
    if card_uid not in food_dict:
        return None
    with lock:
        current_tap = card_uid
        current_tap_time = current_time()
        return food_dict[card_uid][1]

# Define colors
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

font = pygame.font.SysFont('Arial', 30)

# Game loop
def display_loop():
    global current_tap
    global current_tap_time
    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            # Check for key press events
            elif event.type == pygame.KEYDOWN:
                # Check if the key pressed is 'Q' and if the Ctrl key is held down
                if event.key == pygame.K_q and event.mod & pygame.KMOD_CTRL:
                    running = False

        screen.fill(WHITE)  # Fill the screen with white

        tap = None

        with lock:
            if current_tap is not None:
                if current_time() - current_tap_time > 3000:
                    current_tap = None
                else:
                    tap = current_tap

        if tap is not None:
            text_surface = font.render(f'Kitty server: got tap for {food_dict[tap][1]}!', True, BLACK)
            screen.blit(text_surface, (250, 250))
            screen.blit(food_dict[tap][0], (250, 400))
        else:
            text_surface = font.render('Kitty server: waiting on taps...', True, BLACK)
            screen.blit(text_surface, (250, 250))

        pygame.display.flip()  # Update the display

        clock.tick(10)

    pygame.quit()
    sys.exit(0)
