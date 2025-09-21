#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h> 
#include <string.h> 


float cue_angle = 0.0f; 
const float ANGLE_STEP = 0.03f; 
float shot_power = 5.0f; 

#define TIMER_BASE       0xFF202000
#define TIMER_STATUS     (*(volatile int *)(TIMER_BASE))
#define TIMER_CONTROL    (*(volatile int *)(TIMER_BASE + 0x4))
#define TIMER_PERIODL    (*(volatile int *)(TIMER_BASE + 0x8))
#define TIMER_PERIODH    (*(volatile int *)(TIMER_BASE + 0xC))

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define TABLE_WIDTH 290
#define TABLE_HEIGHT 150
#define POCKET_RADIUS 10
#define BALL_RADIUS 7
#define NUM_CIRCLES 15
#define FRICTION 0.984f
	
#define WHITE  0xFFFF
#define YELLOW 0xFFE0
#define RED  0xF800
#define BLUE 0x001F

	
#define PS2_BASE ((volatile int *)0xFF200100)

unsigned char read_ps2_key() {
  int data = PS2_BASE[0];
  if ((data & 0x8000) == 0) return 0; 
  return data & 0xFF;
}

volatile int pixel_buffer_start;
short int Buffer1[240][512];
short int Buffer2[240][512];
int render_counter;

typedef struct {
  float x, y;
  float vx, vy;
  bool visible;
  int team;
} Circle;

Circle circles[NUM_CIRCLES];
Circle cue_ball;
int player1_team = 0; 
int player2_team = 0;
bool teams_assigned = false;
extern bool shot_taken;
extern bool ball_pocketed_this_turn;
extern int current_player;
extern int player1_team;
extern int player2_team;
extern bool teams_assigned;
bool foul_committed = false;
int first_hit_ball_index = -1;
bool cue_ball_has_hit_any_ball = false;
bool space_pressed = false;
bool release_pending = false;
bool space_armed = true; // True if spacebar can trigger input
bool shot_taken = false;
bool ball_pocketed_this_turn = false;
int current_player = 1; // Player 1 or 2
char message_line_0[64] = "";
char message_line_1[64] = "";
short int message_line_0_color = YELLOW;
short int message_line_1_color = WHITE;
bool current_player_scored_own_team_ball = false;


void check_foul();
void wait_for_vsync();
void plot_pixel(int x, int y, short int color);
void clear_screen();
void draw_square();
void plot_circle(int x0, int y0, int radius, short int color);
void initialize_break();
void draw_circles();
void timer_init();
void wait_for_timer();
void update_all();
void update_positions();
void apply_friction();
void update_ball_collisions();
void update_wall_collisions();
void update_pocket_detection();
bool all_balls_stopped();
void input();
void draw_cue_direction();
float find_first_ball_hit(float x, float y, float vx, float vy);
float find_wall_intersection(float x, float y, float vx, float vy, float *nx, float *ny);
void draw_line(int x0, int y0, int x1, int y1, short int color);
bool check_loss();
void switch_player();
bool player_has_remaining_balls(int team);
bool evaluated_after_shot = false;
void post_shot_evaluation();
void assign_teams_if_needed(int pocketed_ball_index);
void detect_first_hit();
void reset_cue_ball_position();
void draw_char(int x, int y, char c, short int color);
void draw_bottom_text_line(const char* text, short int color, int line_number);
short int get_player_color(int player);

int main() {
    // Graphics Init
    volatile int *pixel_ctrl_ptr = (int *)0xFF203020;
    short int Buffer1[240][512], Buffer2[240][512];
    int render_counter = 0;

    *(pixel_ctrl_ptr + 1) = (int)&Buffer1;
    wait_for_vsync();
    pixel_buffer_start = *pixel_ctrl_ptr;
    clear_screen();

    *(pixel_ctrl_ptr + 1) = (int)&Buffer2;
    pixel_buffer_start = *(pixel_ctrl_ptr + 1);
    clear_screen();

    initialize_break();
    timer_init();

    cue_ball = (Circle){((SCREEN_WIDTH + TABLE_WIDTH) / 2) - 200, SCREEN_HEIGHT / 2, 0, 0, true, 0};

    while (1) {
        update_all();
        wait_for_timer();
        render_counter++;

        if (render_counter >= 5) {
            clear_screen();
            draw_square();
            draw_circles();
            draw_cue_direction();
			draw_bottom_text_line(message_line_0, message_line_0_color, 0);
			draw_bottom_text_line(message_line_1, message_line_1_color, 1);
            wait_for_vsync();
            pixel_buffer_start = *(pixel_ctrl_ptr + 1);
            render_counter = 0;
			
        }

        if (all_balls_stopped()) {
            if (!shot_taken) input();
            else post_shot_evaluation();
        } else {
            evaluated_after_shot = false;
        }
    }

    return 0;
}


void clear_screen() {
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      plot_pixel(x, y, 0x0000);
    }
  }
}

void plot_pixel(int x, int y, short int color) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
    return;
  short int *pixel = (short int *)(pixel_buffer_start + (y << 10) + (x << 1));
  *pixel = color;
}

void wait_for_vsync() {
  volatile int *pixel_ctrl_ptr = (int *)0xFF203020;
  *(pixel_ctrl_ptr) = 0x1;
  while (*(pixel_ctrl_ptr + 3) & 0x01);
}

void draw_square() {
  for (int y = (SCREEN_HEIGHT - TABLE_HEIGHT ) / 2; y < (SCREEN_HEIGHT + TABLE_HEIGHT) / 2; y++) {
    for (int x = (SCREEN_WIDTH - TABLE_WIDTH) / 2; x < (SCREEN_WIDTH + TABLE_WIDTH) / 2; x++) {
      plot_pixel(x, y, 0x1300);
    }
  }

  plot_circle((SCREEN_WIDTH - TABLE_WIDTH) / 2, (SCREEN_HEIGHT - TABLE_HEIGHT) / 2, POCKET_RADIUS, 0xBDF7);
  plot_circle((SCREEN_WIDTH - TABLE_WIDTH) / 2, (SCREEN_HEIGHT + TABLE_HEIGHT) / 2, POCKET_RADIUS, 0xBDF7);
  plot_circle((SCREEN_WIDTH + TABLE_WIDTH) / 2, (SCREEN_HEIGHT - TABLE_HEIGHT) / 2, POCKET_RADIUS, 0xBDF7);
  plot_circle((SCREEN_WIDTH + TABLE_WIDTH) / 2, (SCREEN_HEIGHT + TABLE_HEIGHT) / 2, POCKET_RADIUS, 0xBDF7);
  plot_circle(SCREEN_WIDTH / 2, (SCREEN_HEIGHT - TABLE_HEIGHT) / 2, POCKET_RADIUS, 0xBDF7);
  plot_circle(SCREEN_WIDTH / 2, (SCREEN_HEIGHT + TABLE_HEIGHT) / 2, POCKET_RADIUS, 0xBDF7);
}

void plot_circle(int x0, int y0, int radius, short int color) {
  int x = radius;
  int y = 0;
  int err = 0;
  while (x >= y) {
    for (int i = x0 - x; i <= x0 + x; i++) {
      plot_pixel(i, y0 + y, color);
      plot_pixel(i, y0 - y, color);
    }
    for (int i = x0 - y; i <= x0 + y; i++) {
      plot_pixel(i, y0 + x, color);
      plot_pixel(i, y0 - x, color);
    }
    y += 1;
    if (err <= 0) {
      err += 2 * y + 1;
    }
    if (err > 0) {
      x -= 1;
      err -= 2 * x + 1;
    }
  }
}

void draw_circles() {
  int i;
  for (i = 0; i < NUM_CIRCLES; i++) {
    if (!circles[i].visible) continue;

    short int color;
	  
    if (i == 0 || i == 1 || i == 5 || i == 6 || i == 8 || i == 12 || i == 14)
      color = 0x001F; // Blue
    else if (i == 4)
      color = 0x0000; // Black
    else
      color = 0xF800; // Red

    plot_circle((int)circles[i].x, (int)circles[i].y, BALL_RADIUS, color);
  }

  if (cue_ball.visible)
    plot_circle((int)cue_ball.x, (int)cue_ball.y, BALL_RADIUS, 0xFFFF); // White for cue ball
}

void initialize_break() {
    int rows = 5;
    int ball_index = 0;
    int start_x = (SCREEN_WIDTH - TABLE_WIDTH) / 2 + 200;
    int start_y = SCREEN_HEIGHT / 2;

    sprintf(message_line_1, "show us what u have, player 1");
    message_line_1_color = get_player_color(current_player);

    for (int row = 0; row < rows && ball_index < NUM_CIRCLES; row++) {
        for (int col = 0; col <= row && ball_index < NUM_CIRCLES; col++) {
            Circle *c = &circles[ball_index];
            c->x = start_x + row * (2 * BALL_RADIUS * 0.88f);
            c->y = start_y - row * BALL_RADIUS + col * (2 * BALL_RADIUS);
            
            // Assign team based on color logic
            if (ball_index == 0 || ball_index == 1 || ball_index == 5 ||
                ball_index == 6 || ball_index == 8 || ball_index == 12 || ball_index == 14) {
                c->team = 2;  // Blue
            } else if (ball_index == 4) {
                c->team = 0;  // 8-ball
            } else {
                c->team = 1;  // Red
            }

            c->vx = 0;
            c->vy = 0;
            c->visible = true;
            ball_index++;
        }
    }
}

void timer_init() {
  int delay = 5; // For 100000hz at 50MHz clock
  TIMER_PERIODL = delay & 0xFFFF;
  TIMER_PERIODH = (delay >> 16) & 0xFFFF;
  TIMER_CONTROL = 0b0110;
  TIMER_STATUS = 0;
}

void wait_for_timer() {
  while ((TIMER_STATUS & 0x1) == 0);
  TIMER_STATUS = 0;
}

void update_all() {
  update_positions();
  apply_friction();
  update_ball_collisions();
  update_wall_collisions();
  update_pocket_detection();
}

void update_positions() {
  for (int i = 0; i < NUM_CIRCLES; i++) {
    if (circles[i].visible) {
      circles[i].x += circles[i].vx;
      circles[i].y += circles[i].vy;
    }
  }
  if (cue_ball.visible) {
    cue_ball.x += cue_ball.vx;
    cue_ball.y += cue_ball.vy;
  }
}

void apply_friction() {
  for (int i = 0; i < NUM_CIRCLES; i++) {
    circles[i].vx *= FRICTION;
    circles[i].vy *= FRICTION;
  }
  cue_ball.vx *= FRICTION;
  cue_ball.vy *= FRICTION;
}

//this function is helped by chatgpt
void update_ball_collisions() {
    Circle* balls[NUM_CIRCLES + 1];
    for (int i = 0; i < NUM_CIRCLES; i++) balls[i] = &circles[i];
    balls[NUM_CIRCLES] = &cue_ball;

    cue_ball_has_hit_any_ball = false;
    // Don't reset first_hit_ball_index here — we set it ONCE when cue hits something

    for (int i = 0; i <= NUM_CIRCLES; i++) {
        for (int j = i + 1; j <= NUM_CIRCLES; j++) {
            Circle* a = balls[i];
            Circle* b = balls[j];
            if (!a->visible || !b->visible) continue;

            float dx = b->x - a->x;
            float dy = b->y - a->y;
            float dist_sq = dx * dx + dy * dy;
            float min_dist = 2 * BALL_RADIUS;

            if (dist_sq < min_dist * min_dist) {
                // --- physics ---
                float dist = sqrtf(dist_sq);
                if (dist == 0) dist = 0.01f;
                float nx = dx / dist;
                float ny = dy / dist;

                float tx = -ny;
                float ty = nx;

                float va_n = a->vx * nx + a->vy * ny;
                float vb_n = b->vx * nx + b->vy * ny;
                float va_t = a->vx * tx + a->vy * ty;
                float vb_t = b->vx * tx + b->vy * ty;

                float temp = va_n;
                va_n = vb_n;
                vb_n = temp;

                a->vx = va_n * nx + va_t * tx;
                a->vy = va_n * ny + va_t * ty;
                b->vx = vb_n * nx + vb_t * tx;
                b->vy = vb_n * ny + vb_t * ty;

                float overlap = 0.5f * (min_dist - dist);
                a->x -= overlap * nx;
                a->y -= overlap * ny;
                b->x += overlap * nx;
                b->y += overlap * ny;

                // === First Hit Detection ===
                if (first_hit_ball_index == -1) {
                    if (a == &cue_ball && b != &cue_ball && b >= &circles[0] && b < &circles[NUM_CIRCLES]) {
                        first_hit_ball_index = b - &circles[0];
                        cue_ball_has_hit_any_ball = true;
                    } else if (b == &cue_ball && a != &cue_ball && a >= &circles[0] && a < &circles[NUM_CIRCLES]) {
                        first_hit_ball_index = a - &circles[0];
                        cue_ball_has_hit_any_ball = true;
                    }

                    if (first_hit_ball_index != -1) {
                        printf("Cue ball first hit ball %d\n", first_hit_ball_index);
                    }
                }
            }
        }
    }
}

void update_wall_collisions() {
  float left = (SCREEN_WIDTH - TABLE_WIDTH)/2 + BALL_RADIUS;
  float right = (SCREEN_WIDTH + TABLE_WIDTH)/2 - BALL_RADIUS;
  float top = (SCREEN_HEIGHT - TABLE_HEIGHT)/2 + BALL_RADIUS;
  float bottom = (SCREEN_HEIGHT + TABLE_HEIGHT)/2 - BALL_RADIUS;

  Circle* balls[NUM_CIRCLES + 1];
  for (int i = 0; i < NUM_CIRCLES; i++) balls[i] = &circles[i];
  balls[NUM_CIRCLES] = &cue_ball;

  for (int i = 0; i <= NUM_CIRCLES; i++) {
    Circle* c = balls[i];
    if (!c->visible) continue;

    if (c->x < left) {
      c->x = left;
      c->vx = -c->vx; 
    } 
    else if (c->x > right) {
      c->x = right;
      c->vx = -c->vx;
    }

    if (c->y < top) {
      c->y = top;
      c->vy = -c->vy;
    } 
    else if (c->y > bottom) {
      c->y = bottom;
      c->vy = -c->vy;
    }
  }
}

void update_pocket_detection() {
    int pocket_positions[6][2] = {
        {(SCREEN_WIDTH - TABLE_WIDTH) / 2, (SCREEN_HEIGHT - TABLE_HEIGHT) / 2},
        {(SCREEN_WIDTH - TABLE_WIDTH) / 2, (SCREEN_HEIGHT + TABLE_HEIGHT) / 2},
        {(SCREEN_WIDTH + TABLE_WIDTH) / 2, (SCREEN_HEIGHT - TABLE_HEIGHT) / 2},
        {(SCREEN_WIDTH + TABLE_WIDTH) / 2, (SCREEN_HEIGHT + TABLE_HEIGHT) / 2},
        {SCREEN_WIDTH / 2, (SCREEN_HEIGHT - TABLE_HEIGHT) / 2},
        {SCREEN_WIDTH / 2, (SCREEN_HEIGHT + TABLE_HEIGHT) / 2},
    };

    Circle* balls[NUM_CIRCLES + 1];
    for (int i = 0; i < NUM_CIRCLES; i++) balls[i] = &circles[i];
    balls[NUM_CIRCLES] = &cue_ball;

    for (int i = 0; i <= NUM_CIRCLES; i++) {
        Circle* c = balls[i];
        if (!c->visible) continue;

        for (int j = 0; j < 6; j++) {
            float dx = c->x - pocket_positions[j][0];
            float dy = c->y - pocket_positions[j][1];
            float effective_radius = POCKET_RADIUS / 1.4f + BALL_RADIUS / 1.4f;

            if ((dx * dx + dy * dy) <= (effective_radius * effective_radius)) {
                c->visible = false;

                if (i != NUM_CIRCLES) {
                    ball_pocketed_this_turn = true;
                    assign_teams_if_needed(i);

                    int target_team = (current_player == 1) ? player1_team : player2_team;
                    if (teams_assigned && circles[i].team == target_team) {
                        current_player_scored_own_team_ball = true;
                    }
                }

                break;
            }
        }
    }
}

bool all_balls_stopped() {
  for (int i = 0; i < NUM_CIRCLES; i++) {
    if (circles[i].visible && (fabsf(circles[i].vx) > 0.01f || fabsf(circles[i].vy) > 0.01f)) {
      return false;
    }
  }
  if (cue_ball.visible && (fabsf(cue_ball.vx) > 0.01f || fabsf(cue_ball.vy) > 0.01f)) {
    return false;
  }
  return true;
}

void input() {
    unsigned char ps2_code = read_ps2_key();

    if (ps2_code == 0xF0) {
        release_pending = true;
        return;
    }

    if (release_pending) {
        if (ps2_code == 0x29) { // Space released
            space_armed = true;
        }
        release_pending = false;
        return;
    }

    switch (ps2_code) {
        case 0x1D: cue_angle -= ANGLE_STEP; break; // W
        case 0x1B: cue_angle += ANGLE_STEP; break; // S

        case 0x29: // Space
            if (space_armed && !shot_taken && all_balls_stopped()) {
                cue_ball.vx = 1.5 * shot_power * cosf(cue_angle);
                cue_ball.vy = 1.5 * shot_power * sinf(cue_angle);
                shot_taken = true;
                space_armed = false; // Disarm until released
				message_line_0[0] = '\0';
    			message_line_1[0] = '\0';
            }
            break;

        case 0x45: shot_power = 0.0f; break; // 0
        case 0x16: shot_power = 1.0f; break; // 1
        case 0x1E: shot_power = 2.0f; break; // 2
        case 0x26: shot_power = 3.0f; break; // 3
        case 0x25: shot_power = 4.0f; break; // 4
        case 0x2E: shot_power = 5.0f; break; // 5
        case 0x36: shot_power = 6.0f; break; // 6
        case 0x3D: shot_power = 7.0f; break; // 7
        case 0x3E: shot_power = 8.0f; break; // 8
        case 0x46: shot_power = 9.0f; break; // 9
            break;
    }
}

void draw_line(int x0, int y0, int x1, int y1, short int color) {
  int temp;
  int is_steep = 0;

  if (abs(y1 - y0) > abs(x1 - x0)) {
    is_steep = 1;
  }

  if (is_steep) {
    temp = x0; x0 = y0; y0 = temp;
    temp = x1; x1 = y1; y1 = temp;
  }

  if (x0 > x1) {
    temp = x0; x0 = x1; x1 = temp;
    temp = y0; y0 = y1; y1 = temp;
  }

  int deltax = x1 - x0;
  int deltay = abs(y1 - y0);
  int error = -(deltax / 2);
  int y = y0;
  int y_step = (y0 < y1) ? 1 : -1;

  for (int x = x0; x <= x1; x++) {
    if (is_steep)
      plot_pixel(y, x, color);
    else
      plot_pixel(x, y, color);

    error += deltay;
    if (error > 0) {
      y += y_step;
      error -= deltax;
    }
  }
}

float find_wall_intersection(float x, float y, float vx, float vy, float *nx, float *ny) {
  float left = (SCREEN_WIDTH - TABLE_WIDTH)/2 + BALL_RADIUS;
  float right = (SCREEN_WIDTH + TABLE_WIDTH)/2 - BALL_RADIUS;
  float top = (SCREEN_HEIGHT - TABLE_HEIGHT)/2 + BALL_RADIUS;
  float bottom = (SCREEN_HEIGHT + TABLE_HEIGHT)/2 - BALL_RADIUS;

  float t = 1e6;
  *nx = 0; *ny = 0;

  if (vx > 0) {
    float tx = (right - x) / vx;
    if (tx < t) { t = tx; *nx = -1; *ny = 0; }
  } else if (vx < 0) {
    float tx = (left - x) / vx;
    if (tx < t) { t = tx; *nx = 1; *ny = 0; }
  }

  if (vy > 0) {
    float ty = (bottom - y) / vy;
    if (ty < t) { t = ty; *nx = 0; *ny = -1; }
  } else if (vy < 0) {
    float ty = (top - y) / vy;
    if (ty < t) { t = ty; *nx = 0; *ny = 1; }
  }

  return t;
}

float find_first_ball_hit(float x, float y, float vx, float vy) {
  float closest_t = 1e6;

  for (int i = 0; i < NUM_CIRCLES; i++) {
    if (!circles[i].visible) continue;
    Circle* b = &circles[i];

    float dx = b->x - x;
    float dy = b->y - y;
    float proj = dx * vx + dy * vy;
    if (proj <= 0) continue;

    float cx = x + vx * proj;
    float cy = y + vy * proj;
    float dist_sq = (b->x - cx)*(b->x - cx) + (b->y - cy)*(b->y - cy);

    if (dist_sq <= (2 * BALL_RADIUS) * (2 * BALL_RADIUS) && proj < closest_t) {
      closest_t = proj;
    }
  }

  return closest_t;
}

void draw_cue_direction() {
  if (!all_balls_stopped()) return;
  if (!cue_ball.visible) return;

  float x = cue_ball.x;
  float y = cue_ball.y;
  float vx = cosf(cue_angle);
  float vy = sinf(cue_angle);

  const int MAX_BOUNCES = 5;

  for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
    float nx, ny;
    float t_wall = find_wall_intersection(x, y, vx, vy, &nx, &ny);
    float t_ball = find_first_ball_hit(x, y, vx, vy);

    float t = (t_ball < t_wall) ? t_ball : t_wall;
    float x1 = x + vx * t;
    float y1 = y + vy * t;

    draw_line((int)x, (int)y, (int)x1, (int)y1, 0x0000);

    if (t_ball < t_wall) break; // stop at first ball hit

    x = x1;
    y = y1;

    if (nx != 0) vx = -vx;
    if (ny != 0) vy = -vy;
  }
}

bool check_loss() {
    if (!teams_assigned) return false;

    // If black ball (ball 4) is pocketed
    if (!circles[4].visible) {
        int target_team = (current_player == 1) ? player1_team : player2_team;

        if (player_has_remaining_balls(target_team)) {
			sprintf(message_line_0, "loss: player %d pocketed 8 ball!", current_player);
            return true;
        } else {
			sprintf(message_line_0, "player %d wins, ez!", current_player);
            // You could add a win flag or reset logic here
            return false;
        }
    }
    return false;
}

void switch_player() {
  current_player = (current_player == 1) ? 2 : 1;
  sprintf(message_line_1, "switched to player %d's turn", current_player);
  message_line_1_color = get_player_color(current_player);
}

void check_foul() {
    foul_committed = false;

    // Cue ball pocketed
    if (!cue_ball.visible) {
        foul_committed = true;
        sprintf(message_line_0, "foul: cue ball pocketed! resetting.");
        return;
    }

    // Cue ball didn't hit anything
    if (first_hit_ball_index == -1) {
        foul_committed = true;
        sprintf(message_line_0, "foul: cue ball didn't hit any ball!");
        return;
    }

    // No team assigned yet → no foul rules applied
    if (!teams_assigned) return;

    int target_team = (current_player == 1) ? player1_team : player2_team;
    int hit_team = circles[first_hit_ball_index].team;
	printf("Player %d's team: %d | First hit ball: %d (team %d)\n", 
    current_player, target_team, first_hit_ball_index, hit_team);


    // Hit 8-ball first too early
    if (first_hit_ball_index == 4 && player_has_remaining_balls(target_team)) {
        foul_committed = true;
        sprintf(message_line_0, "foul: hit 8-ball before clearing your team balls!");
        return;
    }

    // Hit opponent's ball first
    if (hit_team != 0 && hit_team != target_team) {
        foul_committed = true;
        sprintf(message_line_0, "foul: hit opponent's ball first!");
        return;
    }
}

bool player_has_remaining_balls(int team) {
    for (int i = 0; i < NUM_CIRCLES; i++) {
        if (!circles[i].visible) continue;
        int ball_team = circles[i].team;
        if (ball_team == team) return true;
    }
    return false;
}

void post_shot_evaluation() {
    if (!evaluated_after_shot && all_balls_stopped() && shot_taken) {
        evaluated_after_shot = true;

        
        bool loss = check_loss();

        if (loss) {
            return;
        }
        
		check_foul();
        
		if (!cue_ball.visible) {
            reset_cue_ball_position();
        }
		
		
		
        if (foul_committed) {
    	reset_cue_ball_position();
    	switch_player();
		} else if (ball_pocketed_this_turn && current_player_scored_own_team_ball) {
    	// Continue only if a team ball was pocketed
    	sprintf(message_line_1, "player %d continues.", current_player);
    	message_line_1_color = get_player_color(current_player);
		} else {
    	switch_player();
		}

        // Reset turn state
        shot_taken = false;
        ball_pocketed_this_turn = false;
        first_hit_ball_index = -1;
        cue_ball_has_hit_any_ball = false;
    }
}

void assign_teams_if_needed(int pocketed_ball_index) {
    if (teams_assigned || pocketed_ball_index == 4) return;

    int color = (pocketed_ball_index == 0 || pocketed_ball_index == 1 || pocketed_ball_index == 5 ||
                 pocketed_ball_index == 6 || pocketed_ball_index == 8 || pocketed_ball_index == 12 ||
                 pocketed_ball_index == 14) ? 2 : 1; // 2: blue, 1: red

    if (current_player == 1) {
        player1_team = color;
        player2_team = (color == 1) ? 2 : 1;
    } else {
        player2_team = color;
        player1_team = (color == 1) ? 2 : 1;
    }

    teams_assigned = true;
	sprintf(message_line_1, "player %d assigned to %s balls!", current_player, (color == 1 ? "red" : "blue"));
	message_line_1_color = get_player_color(current_player);
} 

	
void reset_cue_ball_position() {
    cue_ball.x = ((SCREEN_WIDTH + TABLE_WIDTH) / 2) - 200;
    cue_ball.y = SCREEN_HEIGHT / 2;
    cue_ball.vx = 0.0f;
    cue_ball.vy = 0.0f;
    cue_ball.visible = true;
}


short int get_player_color(int player) {
    if (!teams_assigned) return WHITE;

    int team = (player == 1) ? player1_team : player2_team;
    return (team == 1) ? RED : BLUE;
}


const int letter_a_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 48 pixels

const int letter_b_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 52 pixels

const int letter_c_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 36 pixels

const int letter_d_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 48 pixels

const int letter_e_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 52 pixels

const int letter_f_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
}; // 40 pixels

const int letter_g_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 48 pixels

const int letter_h_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 48 pixels

const int letter_i_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {2, 2},
    {3, 2},
    {2, 3},
    {3, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 36 pixels

const int letter_j_pixels[][2] = {
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 32 pixels

const int letter_k_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {4, 2},
    {5, 2},
    {4, 3},
    {5, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {4, 6},
    {5, 6},
    {4, 7},
    {5, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 40 pixels

const int letter_l_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 32 pixels

const int letter_m_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {2, 2},
    {3, 2},
    {2, 3},
    {3, 3},
    {4, 2},
    {5, 2},
    {4, 3},
    {5, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 56 pixels

const int letter_n_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {2, 2},
    {3, 2},
    {2, 3},
    {3, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 48 pixels

const int letter_o_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 40 pixels

const int letter_p_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
}; // 40 pixels

const int letter_q_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {4, 6},
    {5, 6},
    {4, 7},
    {5, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 48 pixels

const int letter_r_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {4, 6},
    {5, 6},
    {4, 7},
    {5, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 48 pixels

const int letter_s_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 40 pixels

const int letter_t_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {2, 2},
    {3, 2},
    {2, 3},
    {3, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
}; // 32 pixels

const int letter_u_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 40 pixels

const int letter_v_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
    {4, 6},
    {5, 6},
    {4, 7},
    {5, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
}; // 36 pixels

const int letter_w_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {0, 4},
    {1, 4},
    {0, 5},
    {1, 5},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {6, 4},
    {7, 4},
    {6, 5},
    {7, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
    {4, 6},
    {5, 6},
    {4, 7},
    {5, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 56 pixels

const int letter_x_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {2, 2},
    {3, 2},
    {2, 3},
    {3, 3},
    {4, 2},
    {5, 2},
    {4, 3},
    {5, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
    {4, 6},
    {5, 6},
    {4, 7},
    {5, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 36 pixels

const int letter_y_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
}; // 32 pixels

const int letter_z_pixels[][2] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {6, 0},
    {7, 0},
    {6, 1},
    {7, 1},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 48 pixels

const int char_colon_pixels[][2] = {
    {2, 2},
    {3, 2},
    {2, 3},
    {3, 3},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
}; // 8 pixels

const int char_1_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {2, 2},
    {3, 2},
    {2, 3},
    {3, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 32 pixels

const int char_2_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {2, 6},
    {3, 6},
    {2, 7},
    {3, 7},
    {0, 8},
    {1, 8},
    {0, 9},
    {1, 9},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
    {6, 8},
    {7, 8},
    {6, 9},
    {7, 9},
}; // 40 pixels

const int char_8_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {4, 0},
    {5, 0},
    {4, 1},
    {5, 1},
    {0, 2},
    {1, 2},
    {0, 3},
    {1, 3},
    {6, 2},
    {7, 2},
    {6, 3},
    {7, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {4, 4},
    {5, 4},
    {4, 5},
    {5, 5},
    {0, 6},
    {1, 6},
    {0, 7},
    {1, 7},
    {6, 6},
    {7, 6},
    {6, 7},
    {7, 7},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
    {4, 8},
    {5, 8},
    {4, 9},
    {5, 9},
}; // 40 pixels

// Pixel coordinate arrays for '!' and '.' in 8x10 format

const int char_exclam_pixels[][2] = {
    {2, 0},
    {3, 0},
    {2, 1},
    {3, 1},
    {2, 2},
    {3, 2},
    {2, 3},
    {3, 3},
    {2, 4},
    {3, 4},
    {2, 5},
    {3, 5},
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
}; // 16 pixels

const int char_period_pixels[][2] = {
    {2, 8},
    {3, 8},
    {2, 9},
    {3, 9},
}; // 4 pixels



void draw_char(int x, int y, char c, short int color) {
    const int (*pixels)[2] = NULL;
    int count = 0;

    switch (c) {
        // letters a-z
        case 'a': pixels = letter_a_pixels; count = sizeof(letter_a_pixels)/sizeof(letter_a_pixels[0]); break;
        case 'b': pixels = letter_b_pixels; count = sizeof(letter_b_pixels)/sizeof(letter_b_pixels[0]); break;
        case 'c': pixels = letter_c_pixels; count = sizeof(letter_c_pixels)/sizeof(letter_c_pixels[0]); break;
        case 'd': pixels = letter_d_pixels; count = sizeof(letter_d_pixels)/sizeof(letter_d_pixels[0]); break;
        case 'e': pixels = letter_e_pixels; count = sizeof(letter_e_pixels)/sizeof(letter_e_pixels[0]); break;
        case 'f': pixels = letter_f_pixels; count = sizeof(letter_f_pixels)/sizeof(letter_f_pixels[0]); break;
        case 'g': pixels = letter_g_pixels; count = sizeof(letter_g_pixels)/sizeof(letter_g_pixels[0]); break;
        case 'h': pixels = letter_h_pixels; count = sizeof(letter_h_pixels)/sizeof(letter_h_pixels[0]); break;
        case 'i': pixels = letter_i_pixels; count = sizeof(letter_i_pixels)/sizeof(letter_i_pixels[0]); break;
        case 'j': pixels = letter_j_pixels; count = sizeof(letter_j_pixels)/sizeof(letter_j_pixels[0]); break;
        case 'k': pixels = letter_k_pixels; count = sizeof(letter_k_pixels)/sizeof(letter_k_pixels[0]); break;
        case 'l': pixels = letter_l_pixels; count = sizeof(letter_l_pixels)/sizeof(letter_l_pixels[0]); break;
        case 'm': pixels = letter_m_pixels; count = sizeof(letter_m_pixels)/sizeof(letter_m_pixels[0]); break;
        case 'n': pixels = letter_n_pixels; count = sizeof(letter_n_pixels)/sizeof(letter_n_pixels[0]); break;
        case 'o': pixels = letter_o_pixels; count = sizeof(letter_o_pixels)/sizeof(letter_o_pixels[0]); break;
        case 'p': pixels = letter_p_pixels; count = sizeof(letter_p_pixels)/sizeof(letter_p_pixels[0]); break;
        case 'q': pixels = letter_q_pixels; count = sizeof(letter_q_pixels)/sizeof(letter_q_pixels[0]); break;
        case 'r': pixels = letter_r_pixels; count = sizeof(letter_r_pixels)/sizeof(letter_r_pixels[0]); break;
        case 's': pixels = letter_s_pixels; count = sizeof(letter_s_pixels)/sizeof(letter_s_pixels[0]); break;
        case 't': pixels = letter_t_pixels; count = sizeof(letter_t_pixels)/sizeof(letter_t_pixels[0]); break;
        case 'u': pixels = letter_u_pixels; count = sizeof(letter_u_pixels)/sizeof(letter_u_pixels[0]); break;
        case 'v': pixels = letter_v_pixels; count = sizeof(letter_v_pixels)/sizeof(letter_v_pixels[0]); break;
        case 'w': pixels = letter_w_pixels; count = sizeof(letter_w_pixels)/sizeof(letter_w_pixels[0]); break;
        case 'x': pixels = letter_x_pixels; count = sizeof(letter_x_pixels)/sizeof(letter_x_pixels[0]); break;
        case 'y': pixels = letter_y_pixels; count = sizeof(letter_y_pixels)/sizeof(letter_y_pixels[0]); break;
        case 'z': pixels = letter_z_pixels; count = sizeof(letter_z_pixels)/sizeof(letter_z_pixels[0]); break;

        // numbers and colon
        case '1': pixels = char_1_pixels; count = sizeof(char_1_pixels)/sizeof(char_1_pixels[0]); break;
        case '2': pixels = char_2_pixels; count = sizeof(char_2_pixels)/sizeof(char_2_pixels[0]); break;
        case '8': pixels = char_8_pixels; count = sizeof(char_8_pixels)/sizeof(char_8_pixels[0]); break;
        case ':': pixels = char_colon_pixels; count = sizeof(char_colon_pixels)/sizeof(char_colon_pixels[0]); break;

        default: return;  // Unsupported character
    }

    for (int i = 0; i < count; i++) {
        plot_pixel(x + pixels[i][0], y + pixels[i][1], color);
    }
}

void draw_bottom_text_line(const char* text, short int color, int line_number) {
    if (line_number < 0 || line_number > 1) return;  // Only support 2 lines: 0 and 1

    const int char_height = 10;
    const int line_spacing = 2;
    const int start_x = 10;

    // Calculate vertical position from bottom of screen
    int start_y = SCREEN_HEIGHT - (2 - line_number) * (char_height + line_spacing);

    int cursor_x = start_x;

    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if (c == ' ') {
            cursor_x += 8;  // space gap
        } else {
            draw_char(cursor_x, start_y, c, color);
            cursor_x += 9;  // character width + 1px gap
        }
    }
}
	
