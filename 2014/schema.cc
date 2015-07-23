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

bool stopnow = false;
void sigterm(int) {
  stopnow = true;
}

using std::vector;

const int players_per_game = 9;
const char * games_filename = "matcher.csv";
const char * players_filename = "spelare.csv";

static inline
bool
test_bit(unsigned mask, int bit) {
  return (mask & (1 << bit)) != 0;
}

static inline
void
set_bit(unsigned & mask, int bit) {
  mask |= (1 << bit);
}

static inline
void
clear_bit(unsigned & mask, int bit) {
  mask &= ~(unsigned)(1 << bit);
}

static inline
unsigned
rand_bit(unsigned mask)
{
  unsigned cnt = 0;
  unsigned list[32] = { 0 };
  for (unsigned i = 0; i < 32; i++)
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
  int count;
};

vector<Player*> players;

struct Game
{
  int round;
  const char * time;
  const char * desc;

  int score;
  int ledare;
  int goalkeeper;
  int count;
  vector<Player*> players;
  unsigned players_mask;
};

vector<Game*> file_games;

struct Stats
{
  Stats() {}
  ~Stats() {}

  vector<int> games_per_player;
  Matrix games_together;

  vector<int> min_together;
  vector<int> cnt_games_together; // [N] == #players that has N games together

  int cnt_goalkeeper;
  int min_score;
  int median_score;
  int max_score;
  int min_ledare;
};

struct Sched
{
  Sched() {}
  ~Sched() {}

  vector<unsigned> players_mask_per_round;
  vector<Game*> games;
  Stats stats;
};

Sched empty_sched;

bool
sort_by_score(const Player * p1, const Player * p2)
{
  return p1->score > p2->score;
}

bool
sort_by_name(const Player * p1, const Player * p2)
{
  return strcmp(p1->name, p2->name) < 0;
}

void strip(char * d)
{
  size_t l = strlen(d);
  if (l == 0)
    return;

  if (d[l-1] == '\n')
    d[l-1] = 0;
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
    p->count = c ? atoi(c) ? 1;
    players.push_back(p);
  }

  free(buf);

  std::sort(players.begin(), players.end(), sort_by_score);

  size_t pn = 0;
  for (Player * p : players) {
    p->index = pn;
    pn++;
  }
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
    g->round = atoi(buf);
    g->time = strdup(t);
    g->desc = strdup(d);
    g->players_mask = 0;
    g->count = 0;
    file_games.push_back(g);
  }

  free(buf);

  int min_round = INT_MAX;
  int max_round = 0;
  for (Game * g : file_games) {
    if (g->round < min_round)
      min_round = g->round;
    if (g->round > max_round)
      max_round = g->round;
  }

  for (Game * g : file_games) {
    g->round -= min_round;
  }
}

void
create_empty_sched()
{
  for (Game * g : file_games) {
    empty_sched.games.push_back(g);
  }

  for (Game * g : file_games) {
    while (empty_sched.players_mask_per_round.size() <= (unsigned)g->round)
      empty_sched.players_mask_per_round.push_back(0);
  }

  for (size_t p = 0; p < players.size(); p++)
    empty_sched.stats.games_per_player.push_back(0);

  empty_sched.stats.games_together.init(players.size());
}

Game*
copy_game(const Game * g)
{
  Game * ng = new Game;
  ng->ledare = 0;
  ng->score = 0;
  ng->desc = g->desc;
  ng->time = g->time;
  ng->round = g->round;
  ng->players_mask = 0;
  ng->goalkeeper = 0;
  ng->count = 0;
  return ng;
}

void
add_player_to_game(Sched * s, Game * g, Player * p)
{
  g->score += p->score;
  g->ledare += p->ledare;
  g->goalkeeper += p->goalkeeper;
  g->count += p->count;
  g->players.push_back(p);
  assert(!test_bit(g->players_mask, p->index));
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
remove_player_from_game(Sched * s, Game * g, Player * p)
{
  g->score -= p->score;
  g->ledare -= p->ledare;
  g->goalkeeper -= p->goalkeeper;
  g->count -= p->count;
  g->players.erase(find(g->players.begin(), g->players.end(), p));
  assert(test_bit(g->players_mask, p->index));
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
  }
  for (Game * g : s->games) {
    Game * ng = copy_game(g);
    ns->games.push_back(ng);

    for (Player * p : g->players) {
      add_player_to_game(ns, ng, p);
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
    }
  }

  int cnt_goalkeeper = 0;
  int min_score = INT_MAX;
  int max_score = 0;
  int min_ledare = INT_MAX;
  vector<int> scores;
  for (Game * g : s->games) {
    scores.push_back(g->score);
    if (g->score < min_score)
      min_score = g->score;
    if (g->score > max_score)
      max_score = g->score;
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

Sched*
create_base_sched()
{
  Sched * s = copy_sched(&empty_sched);

  vector<Game*> & games = s->games;

  vector<Player*> players;
  for (Player * p : ::players) {
    players.push_back(p);
  }

  size_t total = players_per_game * games.size();
  for (size_t i = 0; i < total; ) {

    for (size_t no = 0; no < games.size() && i < total; no++) {
      Game * g = games[no];
      Player * p = get_player(players);
      add_player_to_game(s, g, p);
      i += p->count;
    }

    for (size_t no = 0; no < games.size() && i < total; no++) {
      Game * g = games[games.size() - no - 1];
      Player * p = get_player(players);
      add_player_to_game(s, g, p);
      i += p->count;
    }
  }

  if ((total % players.size()) != 0) {
    size_t extra = players.size() - (total % players.size());
    for (size_t i = 0; i < extra; ) {
      Game * g = games[i];
      Player * p = get_player(players);
      add_player_to_game(s, g, p);
      i += p->count;
    }
  }

  compute_stats(s);

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
  unsigned candidates = 0;
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
  Player * p1 = players[rand_bit(candidates)];

  size_t g0n = rand() % s->games.size();
  Game * g0 = 0;
  for (size_t i = 0; i < s->games.size(); i++) {
    Game * g = s->games[(g0n + i) % s->games.size()];
    if (test_bit(g->players_mask, p0->index)) {
      g0 = g;
      break;
    }
  }

  Game * g1 = 0;
  size_t g1n = rand() % s->games.size();
  for (size_t i = 0; i < s->games.size(); i++) {
    Game * g = s->games[(g1n + i) % s->games.size()];
    if (test_bit(g->players_mask, p1->index)) {
      g1 = g;
      break;
    }
  }

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
    return true;
  }

  unsigned cnt = 0;
  Player * swap[32];
  for (size_t i = 0; i < 32; i++) {
    if (test_bit(candidates, i))
      swap[cnt++] = players[i];
  }

  Player * p2 = 0;
  std::default_random_engine generator;
  std::normal_distribution<double> distribution(0, 50);
  do {
    int dist = distribution(generator);
    if (dist < 0)
      continue;
    size_t pos = rand() % cnt;
    for (size_t n = 0; n < cnt; n++) {
      Player * p = swap[(n + pos) % cnt];
      if (abs(p->score - p1->score) <= dist) {
	p2 = p;
	break;
      }
    }
  } while (p2 == NULL);

  if (test_bit(s->players_mask_per_round[g1->round], p2->index))
    return true;

  if (test_bit(s->players_mask_per_round[g0->round], p1->index))
    return true;

  if (PRINT_SWAP)
    fprintf(stderr, "swap %s:%s and %s:%s\n",
	    p2->name, g0->desc, p1->name, g1->desc);

  remove_player_from_game(s, g0, p2);
  add_player_to_game(s, g1, p2);

  remove_player_from_game(s, g1, p1);
  add_player_to_game(s, g0, p1);

  return true;
}

void
permutate(Sched * s) {

  for (int i = 0; i < 100; i++) {
    if (!perm0(s))
      break;
  }
}

void
print_sched(const Sched * s)
{
  for (Game * g : s->games) {
    sort(g->players.begin(), g->players.end(), sort_by_name);
  }

  for (int round = 0; ; round++) {
    size_t pos = 0;
    for (; pos < s->games.size(); pos++)
      if (s->games[pos]->round == round)
        break;

    if (pos >= s->games.size())
      break;

    printf("%d", round);
    for (Game * g : s->games) {
      if (g->round == round)
        printf(",%s %s", g->time, g->desc);
    }
    printf("\n");
    for (Game * g : s->games) {
      if (g->round == round)
        printf(",score:%d ledare:%d goal: %d", g->score, g->ledare, g->goalkeeper);
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
          printf(",%s", g->players[p]->name);
	  if (g->players[p]->goalkeeper)
	    printf(" (G)");
	  if (g->players[p]->ledare)
	    printf(" (L)");
        } else {
          printf(",");
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
          " min_ledare: %d cnt_goalkeeper: %d\n",
	  s->stats.min_score,
	  s->stats.median_score,
	  s->stats.max_score,
	  s->stats.min_ledare,
          s->stats.cnt_goalkeeper);

  for (Player * p : players) {
    fprintf(stderr, "%s : %d games, ",
            p->name,
            s->stats.games_per_player[p->index]);
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

int
main(int argc, char** argv)
{
  srand(time(0));
  read_players(players_filename);
  read_games(games_filename);
  create_empty_sched();
  signal(SIGTERM, sigterm);

  Sched * s = create_base_sched();
  int chars = 0;
  int streak = 1;
  int wins = 0;
  int loops = 0;
  while (streak++ < 100000 && wins < 100000 && loops++ < 1000000 &&
         stopnow == false) {
    Sched * s2 = copy_sched(s);
    permutate(s2);
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
