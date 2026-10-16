#include <iostream>
#include <random>

// 변수 선언이 아니라, 타일 종류를 나타내는 타입 정의
enum class TileType
{
  Wall,
  Empty
};

constexpr int boardSize = 15;
static_assert(boardSize >= 3 && boardSize % 2 == 1,
  "boardSize must be an odd number >= 3");

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

int main()
{
  std::cout << "Algorithm Day 02 - Maze\n";

  std::cout << std::endl;

  TileType tiles[boardSize][boardSize]{};

  for (int x = 0; x < boardSize; ++x)
  {
    tiles[0][x] = TileType::Wall;
  }

  PrintBoard(tiles);
  std::cout << std::endl;

  for (int y = 0; y < boardSize; ++y)
  {
    for (int x = 0; x < boardSize; ++x)
    {
      // y와 x가 둘 다 홀수면 이 칸을 Empty로 바꾸기
      if (y % 2 == 1 && x % 2 == 1)
      {
        tiles[y][x] = TileType::Empty;
      }
    }
  }

  PrintBoard(tiles);
  std::cout << std::endl;

  //// 첫 번째 방과 오른쪽 방 사이의 벽을 뚫는다.
  //tiles[1][2] = TileType::Empty;

  //PrintBoard(tiles);
  //std::cout << std::endl;

  //// 위 상수를 코드로 표현하면,
  //int roomY = 1;
  //int roomX = 1;

  //// 현재 방의 오른쪽 벽을 뚫는다.
  //tiles[roomY][roomX + 1] = TileType::Empty;


  //for (int roomX = 1; roomX < boardSize - 2; roomX += 2)
  //{
  //  tiles[roomY][roomX + 1] = TileType::Empty;
  //}

  //PrintBoard(tiles);

  for (int roomY = 1; roomY < boardSize - 1; roomY += 2)
  {
    for (int roomX = 1; roomX < boardSize - 2; roomX += 2)
    {
      tiles[roomY][roomX + 1] = TileType::Empty;
    }
  }

  int roomX = boardSize - 2;

  for (int roomY = 1; roomY < boardSize - 2; roomY += 2)
  {
    tiles[roomY + 1][roomX] = TileType::Empty;
  }

  //tiles[y][x + 1] = TileType::Empty; // 오른쪽 벽 뚫기
  //tiles[y + 1][x] = TileType::Empty; // 아래쪽 벽 뚫기

  PrintBoard(tiles);
  std::cout << std::endl;

  ResetBoard(tiles);
  PrintBoard(tiles);
  std::cout << std::endl;

  // 반복문 밖에서 난수 엔진을 한 번 만든다.
  std::random_device seed;
  std::mt19937 random(seed());
  std::bernoulli_distribution chooseEast(0.5);

  for (int y = 1; y < boardSize - 1; y += 2)
  {
    for (int x = 1; x < boardSize - 1; x += 2)
    {
      // 1. 오른쪽 아래 마지막 방: 연결할 방향이 없음
      if (y == boardSize - 2 && x == boardSize - 2)
      {
        continue;
      }

      // 2. 마지막 행: 아래에 방이 없으므로 오른쪽으로 연결
      if (y == boardSize - 2)
      {
        tiles[y][x + 1] = TileType::Empty;
      }
      // 3. 마지막 열: 오른쪽에 방이 없으므로 아래로 연결
      else if (x == boardSize - 2)
      {
        tiles[y + 1][x] = TileType::Empty;
      }
      // 4. 나머지 방: 오른쪽 또는 아래쪽을 무작위 선택
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

  PrintBoard(tiles);
  return 0;
}
