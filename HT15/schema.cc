#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

#include <vector>
#include <algorithm>
#include <iostream>
#include <climits>
#include <random>

#define MAX_PLAYER 32
typedef unsigned mask_t;

bool stopnow = false;
void sigterm(int) {
  stopnow = true;
}

using std::vector;

int max_round = 0;
int player_count = 0;
int games_per_player = 0;
const int players_per_game = 7;
const int rounds_per_team = 2;
const char * games_filename = "matcher.csv";
const char * players_filename = "spelare.csv";

static inline
bool
test_bit(mask_t mask, int bit) {
  return (mask & (1 << bit)) != 0;
}

static inline
void
set_bit(mask_t & mask, int bit) {
  mask |= (1 << bit);
}

static inline
void
clear_bit(mask_t & mask, int bit) {
  mask &= ~(mask_t)(1 << bit);
}

static inline
int
count_bits(mask_t mask) {
  int count = 0;
  while (mask) {
    if (mask & 1)
      count++;
    mask >>= 1;
  }
  return count;
}

static inline
void
or_mask(mask_t & mask, const mask_t & mask1)
{
  mask |= mask1;
}

static inline
unsigned
rand_bit(mask_t mask)
{
  unsigned cnt = 0;
  unsigned list[MAX_PLAYER] = { 0 };
  for (unsigned i = 0; i < MAX_PLAYER; i++)
    if (test_bit(mask, i))
      list[cnt++] = i;
  assert(cnt > 0);
  return list[rand() % cnt];
}

struct Matrix
{
  size_t n;
  int *m;

  Matrix() {
    n = 0;
    m = NULL;
  }

  void init(size_t n) {
    if (n) {
      delete [] m;
    }
    this->n = n;
    this->m = new int[n*n];
    bzero(this->m, n*n*sizeof(int));
  }

  ~Matrix () {
    if (n) {
      delete []m;
    }
  }

  void copyFrom(const Matrix& m) {
    if (n != m.n) {
      init(m.n);
    }
    memcpy(this->m, m.m, n*n*sizeof(this->m[0]));
  }

  int& at(int i, int j) {
    return m[(i * n) + j];
  }

  const int& at(int i, int j) const {
    return m[(i * n) + j];
  }
};

struct Player
{
  int score;
  const char * name;
  int ledare;
  int index;
  int goalkeeper;
  int count_as;
  int lost_games;
  vector<int> mask; // games that he can't play
  int rand_no;
};

vector<Player*> players;

struct Game
{
  int round;
  struct Sched * sched;
  const char * time;
  const char * desc;

  int score;
  int ledare;
  int goalkeeper;
  int count_players;
  vector<Player*> players;
  mask_t players_mask;
  mask_t unavailable_mask;

  int get_score() const {
    return score / count_players;
  }

  int count_available() const;
};

vector<Game*> file_games;

struct Stats
{
  Stats() { swaps = 0; memset(failed_swap, 0, sizeof(failed_swap)); }
  ~Stats() {}

  vector<int> games_per_player;
  Matrix games_together;

  vector<int> min_together;
  vector<int> cnt_games_together; // [N] == #players that has N games together

  int min_games; // min games of a player
  int max_games;

  int cnt_goalkeeper;
  int min_score;
  int median_score;
  int max_score;
  int min_ledare;

  int swaps;
  int failed_swap[5];
};

struct Sched
{
  Sched() {}
  ~Sched() {}

  int count_players;
  vector<mask_t> players_mask_per_round;
  vector<vector<Game*> > games_per_round;
  vector<Game*> games;
  Stats stats;
};

int
Game::count_available() const
{
  mask_t mask = 0;
  or_mask(mask, players_mask);
  or_mask(mask, unavailable_mask);
  or_mask(mask, sched->players_mask_per_round[round]);
  return sched->count_players - count_bits(mask);
}

Sched empty_sched;

bool
sort_by_score(const Player * p1, const Player * p2)
{
  return p1->score > p2->score;
}

bool
sort_by_ledare(const Player * p1, const Player * p2)
{
  if (p1->ledare == p2->ledare)
    return sort_by_score(p1, p2);

  return p1->ledare > p2->ledare;
}

bool
sort_by_name(const Player * p1, const Player * p2)
{
  return strcmp(p1->name, p2->name) < 0;
}

bool
sort_games_by_available(const Game *g1, const Game *g2)
{
  int c1 = g1->count_available();
  int c2 = g2->count_available();
  if (c1 == c2)
    return g1->count_players < g2->count_players;
  return c1 < c2;
}

void strip(char * d)
{
  size_t l = strlen(d);
  if (l == 0)
    return;

  if (d[l-1] == '\n')
    d[l-1] = 0;
}

int games_in_round(const vector<Game *> & games, int round) {
  int cnt = 0;
  for (Game * g : games) {
    if (g->round == round)
      cnt++;
  }
  return cnt;
}

void copy_games_in_round(vector<Game*> & dst,
                         const vector<Game *> & games, int round) {
  for (Game * g : games) {
    if (g->round == round) {
      dst.push_back(g);
    }
  }
}

void remove_round(vector<Game *> & games, int round) {
  vector<Game *> copy;
  for (Game * g : games) {
    if (g->round != round)
      copy.push_back(g);
  }
  games = copy;
}

void
read_players(const char * filename)
{
  char * buf = NULL;
  size_t sz = 0;
  FILE * f = fopen(filename, "r");
  while (getline(&buf, &sz, f) > 0)
  {
    if (buf[0] == '#')
      continue;

    strip(buf);

    char * mask = strchr(buf, ';');
    if (mask) {
      *mask = 0;
      mask++;
    }

    char * lost_count = mask ? strchr(mask, ';') : 0;
    if (lost_count) {
      *lost_count = 0;
      lost_count++;
    }

    char * name = strchr(buf, ',');
    if (name == 0)
      break;
    *name = 0;
    name++;

    char * ledare = strchr(name, ',');
    if (ledare) {
      *ledare = 0;
      ledare++;
    }

    char * g = 0;
    if (ledare && (g = strchr(ledare, ',')) != 0) {
      *g = 0;
      g++;
    }

    char * c = 0;
    if (ledare && g && (c = strchr(g, ',')) != 0) {
      *c = 0;
      c++;
    }

    Player * p = new Player;
    p->score = atoi(buf);
    p->name = strdup(name);
    p->ledare = ledare ? atoi(ledare) : 0;
    p->goalkeeper = g ? atoi(g) : 0;
    p->count_as = c ? atoi(c) : 1;
    p->lost_games = lost_count ? atoi(lost_count) : 0;
    players.push_back(p);
    player_count += p->count_as;

    if (mask) {
      char *endptr;
      do {
        long val = strtol(mask, &endptr, 10);
        if (endptr == mask)
          break;
        p->mask.push_back(val);
        mask = endptr;
      } while (true);
    }
  }

  free(buf);

  std::sort(players.begin(), players.end(), sort_by_score);

  size_t pn = 0;
  for (Player * p : players) {
    p->index = pn;
    pn++;

    for (int m : p->mask) {
      set_bit(file_games[m]->unavailable_mask, p->index);
    }
  }

  size_t total = file_games.size() * players_per_game;
  games_per_player = total / player_count;
}

void
read_games(const char * filename)
{
  char * buf = NULL;
  size_t sz = 0;
  FILE * f = fopen(filename, "r");
  while (getline(&buf, &sz, f) > 0)
  {
    if (buf[0] == '#')
      continue;

    strip(buf);

    char * t = strchr(buf, ';');
    *t = 0;
    t++;
    char * d = strchr(t, ',');
    *d = 0;
    d++;

    Game * g = new Game;
    g->sched = NULL;
    g->round = atoi(buf);
    g->time = strdup(t);
    g->desc = strdup(d);
    g->players_mask = 0;
    g->unavailable_mask = 0;
    g->count_players = 0;
    file_games.push_back(g);
  }

  free(buf);

  int min_round = INT_MAX;
  for (Game * g : file_games) {
    if (g->round < min_round)
      min_round = g->round;
    if (g->round > max_round)
      max_round = g->round;
  }

  for (Game * g : file_games) {
    g->round -= min_round;
  }
  max_round -= min_round;
}

void
create_empty_sched()
{
  for (Game * g : file_games) {
    empty_sched.games.push_back(g);
  }

  for (Game * g : file_games) {
    while (empty_sched.players_mask_per_round.size() <= (unsigned)g->round) {
      empty_sched.players_mask_per_round.push_back(0);
      vector<Game*> tmp;
      empty_sched.games_per_round.push_back(tmp);
    }
  }

  for (size_t p = 0; p < players.size(); p++)
    empty_sched.stats.games_per_player.push_back(0);

  empty_sched.stats.games_together.init(players.size());
}

Game*
copy_game(const Game * g)
{
  Game * ng = new Game;
  ng->sched = NULL;
  ng->ledare = 0;
  ng->score = 0;
  ng->desc = g->desc;
  ng->time = g->time;
  ng->round = g->round;
  ng->players_mask = 0;
  ng->unavailable_mask = g->unavailable_mask;
  ng->goalkeeper = 0;
  ng->count_players = 0;
  return ng;
}

void
add_player_to_game(Game * g, Player * p)
{
  Sched * s = g->sched;
  g->score += p->score;
  g->ledare += !!p->ledare;
  g->goalkeeper += p->goalkeeper;
  g->count_players += p->count_as;
  g->players.push_back(p);
  if (test_bit(g->players_mask, p->index)) {
    printf("assert g->players_mask %s to %s\n", p->name, g->desc);
  }
  assert(!test_bit(g->players_mask, p->index));
  if (test_bit(g->unavailable_mask, p->index)) {
    printf("assert g->unavailable_mask %s to %s\n", p->name, g->desc);
  }
  assert(!test_bit(g->unavailable_mask, p->index));
  set_bit(g->players_mask, p->index);

  assert(!test_bit(s->players_mask_per_round[g->round], p->index));
  set_bit(s->players_mask_per_round[g->round], p->index);
  s->stats.games_per_player[p->index]++;

  for (Player * pp : g->players) {
    s->stats.games_together.at(pp->index, p->index)++;
    s->stats.games_together.at(p->index, pp->index)++;
  }
}

void
remove_player_from_game(Game * g, Player * p)
{
  Sched * s = g->sched;
  g->score -= p->score;
  g->ledare -= !!p->ledare;
  g->goalkeeper -= p->goalkeeper;
  g->count_players -= p->count_as;
  g->players.erase(find(g->players.begin(), g->players.end(), p));
  assert(test_bit(g->players_mask, p->index));
  assert(!test_bit(g->unavailable_mask, p->index));
  clear_bit(g->players_mask, p->index);

  assert(test_bit(s->players_mask_per_round[g->round], p->index));
  clear_bit(s->players_mask_per_round[g->round], p->index);
  s->stats.games_per_player[p->index]--;

  for (Player * pp : g->players) {
    s->stats.games_together.at(pp->index, p->index)--;
    s->stats.games_together.at(p->index, pp->index)--;
  }
}

Sched*
copy_sched(const Sched * s)
{
  Sched * ns = new Sched;
  ns->stats.games_together.init(players.size());
  for (size_t n = 0; n < players.size(); n++) {
    ns->stats.games_per_player.push_back(0);
  }
  for (size_t n = 0; n < s->players_mask_per_round.size(); n++) {
    ns->players_mask_per_round.push_back(0);
    vector<Game*> tmp;
    ns->games_per_round.push_back(tmp);
  }
  for (Game * g : s->games) {
    Game * ng = copy_game(g);
    ng->sched = ns;
    ns->games_per_round[ng->round].push_back(ng);
    ns->games.push_back(ng);

    for (Player * p : g->players) {
      add_player_to_game(ng, p);
    }
  }

  return ns;
}

void
compute_stats(Sched * s) {

  {
    s->stats.cnt_games_together.clear();
    for (size_t n = 0; n < s->games.size(); n++)
      s->stats.cnt_games_together.push_back(0);

    s->stats.min_games = INT_MAX;
    s->stats.max_games = 0;
    for (size_t n = 0; n < players.size() - 1; n++) {
      int min_together = INT_MAX;
      for (size_t m = n + 1; m < players.size(); m++) {
	int val = s->stats.games_together.at(n,m);
        s->stats.cnt_games_together[val]++;
	if (val < min_together) {
	  min_together = val;
	}
	s->stats.min_together.push_back(min_together);
      }

      //TODO remove once player=0 has been fixed
      if (s->stats.games_per_player[players[n]->index] > 0)
      {
        if (s->stats.games_per_player[players[n]->index] < s->stats.min_games)
          s->stats.min_games = s->stats.games_per_player[players[n]->index];
      }

      if (s->stats.games_per_player[players[n]->index] > s->stats.max_games)
        s->stats.max_games = s->stats.games_per_player[players[n]->index];
    }
  }

  int cnt_goalkeeper = 0;
  int min_score = INT_MAX;
  int max_score = 0;
  int min_ledare = INT_MAX;
  vector<int> scores;
  for (Game * g : s->games) {
    scores.push_back(g->get_score());
    if (g->get_score() < min_score)
      min_score = g->get_score();
    if (g->get_score() > max_score)
      max_score = g->get_score();
    if (g->ledare < min_ledare)
      min_ledare = g->ledare;
    if (g->goalkeeper > 0)
      cnt_goalkeeper++;
  }
  std::sort(scores.begin(), scores.end());
  s->stats.min_score = min_score;
  s->stats.median_score = scores[scores.size() / 2];
  s->stats.max_score = max_score;
  s->stats.min_ledare = min_ledare;
  s->stats.cnt_goalkeeper = cnt_goalkeeper;
}

Player*
get_player(vector<Player*> & list)
{
  Player * p = list[0];
  list.erase(list.begin());
  list.push_back(p);
  return p;
}

Game*
get_game(const Sched * s, Player * p)
{
  Game * game = NULL;
  const vector<Game*> & games = s->games;
  for (size_t i = 0; i < games.size(); i++) {
    Game * g = games[i];
    if (test_bit(s->players_mask_per_round[g->round], p->index))
      continue;
    if (test_bit(g->unavailable_mask, p->index))
      continue;
    if (game == NULL || g->count_players < game->count_players)
      game = g;
  }

  return game;
}

int cnt_games(const Sched * s, Player * p)
{
  return s->stats.games_per_player[p->index] + p->lost_games;
}

Player*
get_player(const Sched * s, const vector<Player*> players, Game * g)
{
  Player * player = NULL;
  for (Player * p : players) {
    if (test_bit(s->players_mask_per_round[g->round], p->index))
      continue;
    if (test_bit(g->unavailable_mask, p->index))
      continue;
    if (player == NULL || cnt_games(s, p) < cnt_games(s, player))
      player = p;
  }

  return player;
}

void
print_sched(const Sched * s)
{
  for (Game * g : s->games) {
    sort(g->players.begin(), g->players.end(), sort_by_name);
  }

  for (int round = 0; round <= max_round; round++) {
    size_t pos = 0;
    for (; pos < s->games.size(); pos++)
      if (s->games[pos]->round == round)
        break;

    if (pos >= s->games.size())
      continue;

    printf("%d", round);
    for (Game * g : s->games) {
      if (g->round == round)
        printf(",%s %s,", g->time, g->desc);
    }
    printf("\n");
    for (Game * g : s->games) {
      if (g->round == round)
        printf(",,score:%d ledare:%d goal: %d count: %d", g->get_score(), g->ledare, g->goalkeeper, g->count_players);
    }
    printf("\n");

    size_t p = 0;
    bool done = false;
    while (!done) {
      done = true;
      for (Game * g : s->games) {
        if (g->round != round)
          continue;
        if (g->players.size() > p) {
          done = false;
          printf(",%s,", g->players[p]->name);
	  if (g->players[p]->goalkeeper)
	    printf("(G)");
	  if (g->players[p]->ledare)
	    printf("(L)");
        } else {
          printf(",,");
        }
      }
      printf("\n");
      p++;
    }
  }

  fprintf(stderr, "cnt: ");
  for (size_t n = 0; n < s->stats.cnt_games_together.size(); n++) {
    fprintf(stderr, "%ld-%d, ", n,  s->stats.cnt_games_together[n]);
  }
  fprintf(stderr, "\n");

  fprintf(stderr,
	  "min_score: %d median_score: %d max_score: %d"
          " min_ledare: %d cnt_goalkeeper: %d min/max games: %d/%d\n",
	  s->stats.min_score,
	  s->stats.median_score,
	  s->stats.max_score,
	  s->stats.min_ledare,
          s->stats.cnt_goalkeeper,
          s->stats.min_games,
          s->stats.max_games);

  fprintf(stderr,
          "swaps: %d failed: ",
          s->stats.swaps);
  for (int i : s->stats.failed_swap) {
    fprintf(stderr,
            "%d ",
            i);
  }
  fprintf(stderr, "\n");

  for (Player * p : players) {
    fprintf(stderr, "%s : %d games(%d), ",
            p->name,
            s->stats.games_per_player[p->index],
            p->lost_games);
    for (Player * p2 : players) {
      if (p != p2) {
        fprintf(stderr, "%s:%d ",
                p2->name,
                s->stats.games_together.at(p->index, p2->index));
      }
    }
    fprintf(stderr, "\n");
  }
  fprintf(stderr, "\n");
}

Sched*
create_base_sched()
{
  Sched * s = copy_sched(&empty_sched);

  vector<Game*> & games = s->games;

  int cnt_players = 0;
  vector<Player*> players;
  for (Player * p : ::players) {
    players.push_back(p);
    cnt_players += p->count_as;
  }

  for (Player * p : players) {
    while (s->stats.games_per_player[p->index] + p->lost_games < games_per_player) {
      Game * g = get_game(s, p);
      if (g == NULL)
        break;
      add_player_to_game(g, p);
    }
  }

  for (Game * g : games) {
    while (g->count_players < players_per_game) {
      Player * p = get_player(s, players, g);
      if (p == NULL)
        break;
      add_player_to_game(g, p);
    }
  }

  compute_stats(s);

#if 0
  print_sched(s);
  exit(0);
#endif

  return s;
}

void move_player_to_game(Sched * s, Game * game, vector<Player*> & players,
                         int no)
{
  Player * p = players[no];
  add_player_to_game(game, p);
  players.erase(players.begin() + no);
}

int inc_together(Sched * s, Game * game, Player * player)
{
  int cnt = 0;
  for (Player * p : game->players) {
    if (s->stats.games_together.at(p->index, player->index) == 0)
      cnt++;
  }
  return cnt;
}

int find_player(Sched * s, Game * game, vector<Player*> players)
{
  vector<int> filter1;
  for (size_t i = 0; i < players.size(); i++) {
    Player * p = players[i];
    if (test_bit(game->unavailable_mask, p->index))
      continue;

    filter1.push_back(i);
  }

  int max_inc = 0;
  vector<int> filter2;
  for (int pi : filter1) {
    Player * p = players[pi];
    int inc = inc_together(s, game, p);
    if (inc > max_inc) {
      filter2.clear();
      filter2.push_back(pi);
      max_inc = inc;
    } else if (inc == max_inc) {
      filter2.push_back(pi);
    }
  }

  if (filter2.size() == 0)
    return -1;

  return filter2[0];
}

bool
sort_by_score_rand(const Player * p1, const Player * p2)
{
  if (p1->score != p2->score)
    return p1->score > p2->score;
  return p1->rand_no > p2->rand_no;
}


void rand_players(vector<Player*> & players)
{
  for (Player * p : players) {
    p->rand_no = rand();
  }
  std::sort(players.begin(), players.end(), sort_by_score_rand);
}

void copy_players(Sched * s, Game * dst, const Game * src)
{
  for (Player * p : src->players) {
    if (!test_bit(dst->unavailable_mask, p->index))
      add_player_to_game(dst, p);
  }
}

int fun(int no, int range) {
  /* range 3
    i = 0 => 0
    i = 1 => 1
    i = 2 => 2
    i = 3 0 => 2
    i = 4 1 => 1
    i = 5 2 => 0
    i = 6 => 0
  */
  no %= (2 * range);
  if (no < range) {
    return no;
  } else {
    return (range - 1) - (no % range);
  }
}

int min_players(const vector<Game*> games) {
  int min = INT_MAX;
  for (Game * g : games) {
    if (g->count_players < min)
      min = g->count_players;
  }
  return min;
}

bool too_many_players(const vector<Game*> & games, int limit) {
  for (Game * g : games) {
    if (g->count_players > limit) {
      return true;
    }
  }
  return false;
}

Sched*
create_base_sched2()
{
  Sched * s = copy_sched(&empty_sched);

  for (int round = 0; round <= max_round; round += rounds_per_team) {
    vector<Game*> games;
    copy_games_in_round(games, s->games, round);
    if (games.size() == 0) {
      continue;
    }

    vector<Player*> players = ::players;
    int p = round % players.size();
    while (test_bit(games[0]->unavailable_mask, players[p]->index))
      p++;

    move_player_to_game(s, games[0], players, p);
    rand_players(players);
    for (int i = 1; players.size() ; i++) {
      Game * g = games[fun(i, games.size())];
      int p = find_player(s, g, players);
      if (p == -1)
        break;
      move_player_to_game(s, g, players, p);
    }

    for (int copy = 1; copy < rounds_per_team; copy++) {
      vector<Game*> copy_games;
      copy_games_in_round(copy_games, s->games, round + copy);
      if (copy_games.size() == 0) {
        continue;
      }

      for (size_t i = 0; i < games.size(); i++) {
        copy_players(s, copy_games[i], games[i]);
      }

      vector<Player*> copy2 = players;
      for (int i = 0; copy2.size() ; i++) {
        Game * g = copy_games[fun(i, copy_games.size())];
        int p = find_player(s, g, copy2);
        if (p == -1)
          break;
        move_player_to_game(s, g, copy2, p);
      }
    }
  }

  size_t total = file_games.size() * players_per_game;
  int min_games_per_player = games_per_player;

  for (int pi = 0; too_many_players(s->games, players_per_game); pi++) {
    Player * p = players[fun(pi, players.size())];
    if (s->stats.games_per_player[p->index] < min_games_per_player) {
      continue;
    }

    vector<Game*> games;
    for (Game * g : s->games) {
      if (g->count_players <= players_per_game)
        continue;
      if (!test_bit(g->players_mask, p->index))
        continue;
      int score = g->get_score();
      if (games.empty() || score > games[0]->get_score()) {
        games.clear();
        games.push_back(g);
      } else if (score == games[0]->get_score()) {
        games.push_back(g);
      }
    }

    if (games.empty())
      continue;

    Game * g = games[rand() % games.size()];
    remove_player_from_game(g, p);
  }

  compute_stats(s);

#if 0
  print_sched(s);
  exit(0);
#endif

  return s;
}

struct sched_player
{
  Sched * s;
  Player * p;
};

bool
sort_players_by_score(const sched_player p1, const sched_player p2)
{
  int c1 = p1.p->lost_games + p1.s->stats.games_per_player[p1.p->index];
  int c2 = p2.p->lost_games + p2.s->stats.games_per_player[p2.p->index];
  if (c1 != c2)
    return c1 < c2;
  return sort_by_score(p1.p, p2.p);
}

/**
 * 1) Find game with least available players (and not full)
 * 2) Pick available players
 * 3) Sort players according to
 * - games played
 * - rank
 * 4) Pick player randomly, sorted hightest = most probable
 * 5) Add players=0 players
 */
Sched*
create_base_sched3()
{
  std::default_random_engine generator;
  generator.seed(time(0));
  Sched * s = copy_sched(&empty_sched);

  int cnt_players = 0;
  vector<Player*> players;
  for (Player * p : ::players) {
    if (p->count_as > 0)
      players.push_back(p);
    cnt_players += p->count_as;
  }

  vector<Game*> games = s->games;

  while (games.size()) {
    std::sort(games.begin(), games.end(), sort_games_by_available);
    Game * g = games[0];

    vector<sched_player> possible;
    for (Player * p : players) {
      if (test_bit(g->unavailable_mask, p->index))
        continue;
      if (test_bit(s->players_mask_per_round[g->round], p->index))
        continue;
      sched_player sp = { s, p };
      possible.push_back(sp);
    }
    assert(possible.size() > 0);
    sort(possible.begin(), possible.end(), sort_players_by_score);

    std::normal_distribution<double> distribution(0, possible.size() / 2);
    do
    {
      int val = distribution(generator);
      if (val < 0)
        val = -val;
      if (val >= possible.size())
        continue;

      Player * p = possible[val].p;
      add_player_to_game(g, p);
    } while (0);

    if (g->count_players >= players_per_game)
      games.erase(games.begin());
  }

  compute_stats(s);

#if 0
  print_sched(s);
  exit(0);
#endif

  return s;
}

int
pct(int val1, int val2)
{
  return (100 * (val1 - val2)) / val1;
}

int
compare(const Sched * s1, const Sched * s2) {
  int res;

#define PRINT_COMPARE 0
  if (s1->stats.min_games < games_per_player
      && s2->stats.min_games >= games_per_player)
  {
    return +1;
  }

  if (s1->stats.min_games >= games_per_player
      && s2->stats.min_games < games_per_player)
  {
    return -1;
  }

  if (s1->stats.max_games < games_per_player + 2
      && s2->stats.max_games >= games_per_player + 2)
  {
    return +1;
  }

  if (s1->stats.max_games >= games_per_player + 2
      && s2->stats.max_games < games_per_player + 2)
  {
    return -1;
  }

  if (s1->stats.min_ledare < 2 && s2->stats.min_ledare >= 2) {
    if (PRINT_COMPARE)
      fprintf(stderr, "%u return +1\n", __LINE__);
    return +1;
  }

  if (s1->stats.min_ledare >= 2 && s2->stats.min_ledare < 2) {
    if (PRINT_COMPARE)
      fprintf(stderr, "%u return +1\n", __LINE__);
    return -1;
  }

  if (s1->stats.min_together[0] == 0 && s2->stats.min_together[0] > 0) {
    return +1;
  }

  if (s1->stats.min_together[0] > 0 && s2->stats.min_together[0] == 0)
    return -1;

  if (s1->stats.cnt_goalkeeper > s2->stats.cnt_goalkeeper)
    return -1;

  if (s2->stats.cnt_goalkeeper < s2->stats.cnt_goalkeeper)
    return +1;

  int not_together = abs(s1->stats.min_together[0] -
                         s2->stats.min_together[0]);
  if (not_together > 5)
    return s2->stats.min_together[0] - s1->stats.min_together[0];

  int min_pct = pct(s1->stats.min_score, s2->stats.min_score);
  if (abs(min_pct) > 10)
    return s2->stats.min_score - s1->stats.min_score;

  int med_pct = pct(s1->stats.median_score, s2->stats.median_score);
  if (abs(med_pct) > 10)
    return s2->stats.median_score - s1->stats.median_score;

  res = s1->stats.cnt_games_together[0] - s2->stats.cnt_games_together[0];

  if (abs(res) > 10) {
    if (PRINT_COMPARE)
      fprintf(stderr, "%u return %d\n", __LINE__, res);
    return res;
  }

  int max_pct = pct(s1->stats.max_score, s2->stats.max_score);
  if (abs(max_pct) > 10)
    return s2->stats.max_score - s1->stats.max_score;

  if (PRINT_COMPARE)
    fprintf(stderr, "%u return %d\n", __LINE__, 0);
  return 0;
}

// Find 2 player that never play together
// move 1 of them so that they do play one game together
bool
perm0(Sched * s)
{
  mask_t candidates = 0;
  for (size_t n = 0; n < players.size(); n++) {
    for (size_t m = n + 1; m < players.size(); m++) {
      if (s->stats.games_together.at(n,m) == 0) {
	set_bit(candidates, n);
	set_bit(candidates, m);
      }
    }
  }

  if (candidates == 0)
    return false;

  Player * p0 = players[rand_bit(candidates)];
  candidates = 0;
  for (size_t m = 0; m < players.size(); m++) {
    if (m == (unsigned)p0->index)
      continue;
    if (s->stats.games_together.at(m, p0->index) == 0) {
      set_bit(candidates, m);
    }
  }

  size_t g0n = rand() % s->games.size();
  Game * g0 = 0;
  for (size_t i = 0; i < s->games.size(); i++) {
    Game * g = s->games[(g0n + i) % s->games.size()];
    if (test_bit(g->players_mask, p0->index)) {
      g0 = g;
      break;
    }
  }

  Player * p1 = players[rand_bit(candidates)];
  Game * g1 = 0;
  size_t g1n = rand() % s->games.size();
  for (size_t i = 0; i < s->games.size(); i++) {
    Game * g = s->games[(g1n + i) % s->games.size()];
    if (test_bit(g->players_mask, p1->index)) {
      g1 = g;
      break;
    }
  }

  if (g0 == NULL || g1 == NULL)
    return false;

  // move p1 from g1 to g0...
  // find player p2 in g0 that will swap with p1
  candidates = g0->players_mask;
  // p0 should not swap
  clear_bit(candidates, p0->index);
  // none of the players in g0 can swap
  candidates &= ~s->players_mask_per_round[g1->round];

#define PRINT_SWAP 0
  if (candidates == 0) {
    if (PRINT_SWAP)
      fprintf(stderr, "dont swap %s:%s and %s:%s\n",
	      p0->name, g0->desc, p1->name, g1->desc);
    s->stats.failed_swap[0]++;
    return true;
  }

  unsigned cnt = 0;
  Player * swap[MAX_PLAYER];
  for (size_t i = 0; i < MAX_PLAYER; i++) {
    if (test_bit(candidates, i))
      if (players[i]->count_as <= p1->count_as)
        if (!test_bit(g1->unavailable_mask, players[i]->index))
          swap[cnt++] = players[i];
  }

  int count = p1->count_as;

  if (cnt < (unsigned)count) {
    s->stats.failed_swap[1]++;
    return true;
  }

  int found = 0;
  Player * p2[MAX_PLAYER] = { 0 };
  std::default_random_engine generator;
  std::normal_distribution<double> distribution(0, 50);

  do {
    int dist = distribution(generator);
    if (dist < 0)
      continue;
    size_t pos = rand() % cnt;
    for (size_t n = 0; n < cnt; n++) {
      size_t i = (n + pos) % cnt;
      Player * p = swap[i];
      if (p == NULL)
        continue;
      if (abs(p->score - p1->score) <= dist) {
	p2[found++] = p;
        swap[i] = NULL;
	break;
      }
    }
  } while (found < count);

  for (int i = 0; i < found; i++) {
    if (test_bit(s->players_mask_per_round[g1->round], p2[i]->index)) {
      s->stats.failed_swap[2]++;
      return true;
    }
  }

  if (test_bit(s->players_mask_per_round[g0->round], p1->index)) {
    s->stats.failed_swap[3]++;
    return true;
  }

  if (test_bit(g0->unavailable_mask, p1->index)) {
    s->stats.failed_swap[4]++;
    return true;
  }

  if (PRINT_SWAP)
    fprintf(stderr, "swap %s:%s and %s:%s\n",
	    p2[0]->name, g0->desc, p1->name, g1->desc);

  for (int i = 0; i < found; i++) {
    remove_player_from_game(g0, p2[i]);
    add_player_to_game(g1, p2[i]);
  }

  remove_player_from_game(g1, p1);
  add_player_to_game(g0, p1);

  s->stats.swaps++;

  return true;
}

void
permutate(Sched * s) {

  for (int i = 0; i < 100; i++) {
    if (!perm0(s))
      break;
  }
}

int
main(int argc, char** argv)
{
  srand(time(0));
  read_games(games_filename);
  read_players(players_filename);
  create_empty_sched();
  signal(SIGINT, sigterm);
  signal(SIGTERM, sigterm);

  Sched * base = create_base_sched3();
  Sched * s = copy_sched(base);
  compute_stats(s);
  int chars = 0;
  int streak = 1;
  int wins = 0;
  int loops = 0;
  while (streak++ < 500000 && wins < 100000 && loops++ < 1000000 &&
         stopnow == false) {
    Sched * s2 = NULL;
    switch(streak % 4) {
    case 0:
      s2 = copy_sched(s);
      perm0(s2);
      break;
    case 1:
      s2 = copy_sched(base);
      perm0(s2);
      break;
    case 2:
    case 3:
      s2 = create_base_sched3();
      break;
    case 4:
      s2 = create_base_sched();
      break;
    }
    compute_stats(s2);
    int res = compare(s, s2);
    if (res < 0) {
      wins = 0;
      delete s2;
    } else if (res == 0) {
      wins++;
      delete s2;
    } else {
      wins = 0;
      streak = 1;
      delete s;
      s = s2;
    }

    if (res > 0) {
      chars++;
      fputs("L", stderr);
    } else if ((streak % 100) == 0) {
      chars++;
      fputs("#", stderr);
    } else if ((loops % 100) == 0) {
      chars++;
      fputs(".", stderr);
    }

    if (chars == 79) {
      fputs("\n", stderr);
      chars = 0;
    }
  }
  fputs("\n", stderr);
  fprintf(stderr, "streak: %d loops: %d\n", streak, loops);

  print_sched(s);

  return 0;
}
