#include <iostream>
#include <random>
#include <cstdlib>
using namespace std;

// 변수 선언이 아니라, 타일 종류를 나타내는 타입 정의
enum class TileType
{
  Wall,
  Empty
};

enum class Direction
{
  Up,
  Right,
  Down,
  Left
};

constexpr int boardSize = 15;
static_assert(boardSize >= 3 && boardSize % 2 == 1, "boardSize must be an odd number >= 3");

void PrintBoard(const TileType(&tiles)[boardSize][boardSize], int playerY = -1, int playerX = -1, Direction playerDir = Direction::Up)
{
  for (int y = 0; y < boardSize; ++y)
  {
    for (int x = 0; x < boardSize; ++x)
    {
      if (y == playerY && x == playerX)
      {
        if (playerDir == Direction::Up)
        {
          std::cout << '^';
        }
        else if (playerDir == Direction::Right)
        {
          std::cout << '>';
        }
        else if (playerDir == Direction::Down)
        {
          std::cout << 'v';
        }
        else if (playerDir == Direction::Left)
        {
          std::cout << '<';
        }
      }
      else if (tiles[y][x] == TileType::Wall)
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
}

// 오른손 법칙으로 한 차례 이동하거나 회전한다.
// 참조(&)로 받아 호출한 쪽의 위치와 방향을 변경한다.
void StepPlayer(const TileType(&tiles)[boardSize][boardSize], int& playerY, int& playerX, Direction& playerDir)
{
  Direction rightDir = static_cast<Direction>((static_cast<int>(playerDir) + 1) % 4);

  // 플레이어 기준 오른쪽 칸의 좌표를 계산한다.
  int rightY = playerY;
  int rightX = playerX;

  if (rightDir == Direction::Up)
  {
    rightY -= 1;
  }
  else if (rightDir == Direction::Right)
  {
    rightX += 1;
  }
  else if (rightDir == Direction::Down)
  {
    rightY += 1;
  }
  else if (rightDir == Direction::Left)
  {
    rightX -= 1;
  }

  bool canMoveRight = false;

  if (rightY >= 0 && rightY < boardSize && rightX >= 0 && rightX < boardSize)
  {
    canMoveRight = (tiles[rightY][rightX] == TileType::Empty);
  }

  if (canMoveRight)
  {
    playerDir = rightDir;
    playerY = rightY;
    playerX = rightX;
  }
  else
  {
    // 오른쪽이 막혔으니 앞쪽 칸을 확인한다.
    int nextY = playerY;
    int nextX = playerX;

    if (playerDir == Direction::Up)
    {
      nextY -= 1;
    }
    else if (playerDir == Direction::Right)
    {
      nextX += 1;
    }
    else if (playerDir == Direction::Down)
    {
      nextY += 1;
    }
    else if (playerDir == Direction::Left)
    {
      nextX -= 1;
    }

    if (nextY >= 0 && nextY < boardSize && nextX >= 0 && nextX < boardSize)
    {
      if (tiles[nextY][nextX] == TileType::Empty)
      {
        playerY = nextY;
        playerX = nextX;
      }
      else
      {
        // 오른쪽과 앞쪽이 모두 벽이면 제자리에서 왼쪽으로 회전한다.
        playerDir = static_cast<Direction>((static_cast<int>(playerDir) + 3) % 4);
      }
    }
  }
}

int main()
{
  std::system("cls");
  std::cout << "Algorithm Day 02 - Maze\n\n";

  TileType tiles[boardSize][boardSize]{};
  GenerateMaze(tiles);

  int playerY = 1;
  int playerX = 1;
  Direction playerDir = Direction::Up;

  int goalY = boardSize - 2;
  int goalX = boardSize - 2;

  PrintBoard(tiles, playerY, playerX, playerDir);

  while (playerY != goalY || playerX != goalX)
  {
    std::cout << "\nPress Enter for the next action.\n";
    if (!std::cin.get())
    {
      return 0;
    }

    StepPlayer(tiles, playerY, playerX, playerDir);
    // Windows 콘솔을 지우고 현재 미로만 다시 출력한다.
    std::system("cls");
    std::cout << "Algorithm Day 02 - Maze\n\n";
    PrintBoard(tiles, playerY, playerX, playerDir);
  }

  std::cout << "\nGoal reached!\n";

  return 0;
}
