#include <iostream>
#include <random>
using namespace std;

// 문서의 구현 순서대로 직접 작성해보자.
// 1. 미로 초기화와 출력
// 2. 방 사이 벽 뚫기
// 3. 플레이어 위치와 방향
// 4. 벽 검사와 오른손 법칙
// 5. 엔터 입력마다 한 차례 행동

enum class TileType
{
  Wall,
  Empty
};

constexpr int boardSize = 15;
TileType tiles[boardSize][boardSize]{};

// 미로 출력 
void PrintBoard(const TileType(&tiles)[boardSize][boardSize])
{
  for (int y = 0; y < boardSize; ++y)
  {
    for (int x = 0; x < boardSize; ++x)
    {
      if (tiles[y][x] == TileType::Wall)
      {
        std::cout << '#';
      }
      else if (tiles[y][x] == TileType::Empty)
      {
        std::cout << ' ';
      }
    }

    std::cout << '\n';
  }
}


void ResetBoard(TileType(&tiles)[boardSize][boardSize])
{
  /*
   * y와 x가 모두 홀수이면 빈 방으로 만든다.
   * 나머지는 벽으로 만든다.
   */

  for (int y = 0; y < boardSize; ++y)
  {
    for (int x = 0; x < boardSize; ++x)
    {
      if (y % 2 == 1 && x % 2 == 1)
      {
        tiles[y][x] = TileType::Empty;
      }
      else
      {
        tiles[y][x] = TileType::Wall;
      }
    }
  }
}

// 방을 초기화하고 무작위로 연결해서 미로를 만든다.
void GenerateMaze(TileType(&tiles)[boardSize][boardSize])
{
  ResetBoard(tiles);

  // 반복문 밖에서 난수 엔진을 한 번 만든다.
  std::random_device seed;
  std::mt19937 random(seed());
  std::bernoulli_distribution chooseEast(0.5);

  for (int y = 1; y < boardSize - 1; y += 2)
  {
    for (int x = 1; x < boardSize - 1; x += 2)
    {
      /*
       * 체크해야될 사항
       * 1. 오른쪽 아래 마지막 방
       * 2. 마지막 행의 방
       * 3. 마지막 열의 방
       * 4. 그 밖의 방
       */

      // 1. 오른쪽 아래 마지막 방
      if (y == boardSize - 2 && x == boardSize - 2)
      {
        continue;
      }

      // 2. 마지막 행의 방
      if (y == boardSize - 2)
      {
        tiles[y][x + 1] = TileType::Empty;
      }
      // 3. 마지막 열의 방
      else if (x == boardSize - 2)
      {
        tiles[y + 1][x] = TileType::Empty;
      }
      else if (chooseEast(random))
      {
        tiles[y][x + 1] = TileType::Empty;
      }
      else
      {
        tiles[y + 1][x] = TileType::Empty;
      }
    }
  }
}

int main()
{
  std::cout << "Algorithm Day 02 - Practice\n";

  cout << endl;

  // 여기서부터 직접 구현한다.
  GenerateMaze(tiles);
  PrintBoard(tiles);

  return 0;
}
