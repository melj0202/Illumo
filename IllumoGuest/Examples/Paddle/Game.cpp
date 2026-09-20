#include <IllumoGuest/Application.h>
#include <algorithm>

class PaddleGame final : public GuestApplication
{
public:
  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render),
             "illumo.paddle",
             "illumo.paddle.palette.v1" };
  }
  bool start(std::span<const std::byte> startup) override
  {
    reset();
    return startup.empty();
  }
  void update(const GuestInput& input) override
  {
    width = static_cast<float>(input.width);
    height = static_cast<float>(input.height);
    if (input.consoleOpen) {
      return;
    }
    const float dt = static_cast<float>(std::min(input.elapsed, 0.05));
    if (input.keys[static_cast<std::size_t>(GuestKey::Space)] ==
        GuestKeyAction::Press) {
      reset();
    }
    if (input.held(GuestKey::Left) || input.held(GuestKey::A)) {
      paddle -= dt * 600;
    }
    if (input.held(GuestKey::Right) || input.held(GuestKey::D)) {
      paddle += dt * 600;
    }
    if (input.held(GuestKey::MouseLeft)) {
      paddle = static_cast<float>(input.mouseX);
    }
    const float half = std::min(65.0f, width * 0.2f);
    paddle = std::clamp(paddle, half, std::max(half, width - half));
    if (lost || remaining == 0) {
      return;
    }
    const float oldY = ballY;
    ballX += velocityX * dt;
    ballY += velocityY * dt;
    if (ballX < 8) {
      ballX = 8;
      velocityX = std::abs(velocityX);
    }
    if (ballX > width - 8) {
      ballX = width - 8;
      velocityX = -std::abs(velocityX);
    }
    if (ballY < 8) {
      ballY = 8;
      velocityY = std::abs(velocityY);
    }
    const float paddleY = height - 50;
    if (velocityY > 0 && oldY + 8 <= paddleY && ballY + 8 >= paddleY &&
        std::abs(ballX - paddle) <= half + 8) {
      ballY = paddleY - 8;
      velocityY = -std::abs(velocityY);
      velocityX = (ballX - paddle) * 5;
    }
    if (ballY > height + 8) {
      lost = true;
    }
    for (std::size_t index = 0; index < bricks.size(); ++index) {
      if (!bricks[index]) {
        continue;
      }
      const float x = 20 + static_cast<float>(index % 10) * brickWidth();
      const float y = 40 + static_cast<float>(index / 10) * 28;
      if (ballX + 8 >= x && ballX - 8 <= x + brickWidth() - 4 &&
          ballY + 8 >= y && ballY - 8 <= y + 20) {
        bricks[index] = false;
        --remaining;
        velocityY = -velocityY;
        break;
      }
    }
  }
  GuestFrame frame() override
  {
    GuestFrame result;
    result.width = width;
    result.height = height;
    GuestBatch batch;
    batch.mvp = { 2 / width, 0, 0,  0, 0,  -2 / height, 0, 0,
                  0,         0, -1, 0, -1, 1,           0, 1 };
    rect(batch, 0, 0, width, height, 0xff211912);
    for (std::size_t index = 0; index < bricks.size(); ++index) {
      if (bricks[index]) {
        rect(batch,
             20 + static_cast<float>(index % 10) * brickWidth(),
             40 + static_cast<float>(index / 10) * 28,
             brickWidth() - 4,
             20,
             index < 10   ? 0xff778eff
             : index < 20 ? 0xff9fd16c
                          : 0xffefca61);
      }
    }
    const float half = std::min(65.0f, width * 0.2f);
    rect(batch, paddle - half, height - 50, half * 2, 12, paddleColor);
    rect(batch, ballX - 8, ballY - 8, 16, 16, 0xfff8f1e6);
    rect(batch,
         20,
         height - 20,
         (width - 40) * static_cast<float>(30 - remaining) / 30,
         4,
         lost ? 0xff6868f0 : 0xff9fd16c);
    if (lost || remaining == 0) {
      rect(batch,
           width * 0.25f,
           height * 0.45f,
           width * 0.5f,
           8,
           lost ? 0xff6868f0 : 0xff9fd16c);
    }
    result.batches.push_back(std::move(batch));
    return result;
  }
  bool close() override { return true; }
  void shutdown() override {}
  std::vector<std::byte> extensionRequest() override
  {
    GuestWireWriter request;
    request.u32(0x31444150);
    request.u32(30 - remaining);
    return request.take();
  }
  std::vector<std::byte> receive(std::span<const std::byte> message) override
  {
    GuestWireReader input(message);
    const std::uint32_t magic = input.u32();
    const std::uint32_t color = input.u32();
    if (magic == 0x31444150 && input.finished()) {
      paddleColor = color | 0xff000000u;
    }
    return {};
  }

private:
  float brickWidth() const { return std::max(1.0f, (width - 40) / 10); }
  void reset()
  {
    bricks.fill(true);
    remaining = 30;
    lost = false;
    paddle = width * 0.5f;
    ballX = paddle;
    ballY = height * 0.6f;
    velocityX = 190;
    velocityY = 260;
  }
  static void rect(GuestBatch& batch,
                   float x,
                   float y,
                   float w,
                   float h,
                   std::uint32_t color)
  {
    const std::uint32_t base =
      static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.push_back({ { x, y, 0 }, color, {} });
    batch.vertices.push_back({ { x + w, y, 0 }, color, {} });
    batch.vertices.push_back({ { x + w, y + h, 0 }, color, {} });
    batch.vertices.push_back({ { x, y + h, 0 }, color, {} });
    for (std::uint32_t index : { 0u, 1u, 2u, 2u, 3u, 0u }) {
      batch.indices.push_back(base + index);
    }
  }
  float width = 1280;
  float height = 720;
  float paddle = 640;
  float ballX = 640;
  float ballY = 400;
  float velocityX = 190;
  float velocityY = 260;
  std::array<bool, 30> bricks{};
  unsigned int remaining = 30;
  bool lost = false;
  std::uint32_t paddleColor = 0xffede2ce;
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<PaddleGame>();
}
