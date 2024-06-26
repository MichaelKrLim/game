#include "Game.hpp"

#include "Enemy.hpp"
#include "Player.hpp"
#include "rl/maths.hpp"
#include "rl/operator_overloads.hpp"

#include <unordered_set>

void Game::update_camera(std::chrono::nanoseconds del_time)
{
	constexpr std::chrono::nanoseconds second = std::chrono::seconds{1};
	const auto seconds_passed = static_cast<float>(del_time.count())/second.count();

	camera_position_ += camera_velocity_;
	const auto camera_displacement = player_position()-camera_position_;
	camera_velocity_ = camera_velocity_*camera_velocity_dampening_multiplier_+(seconds_passed*camera_velocity_multiplier_)*camera_displacement;
	
	double scaled_screen_width = GetScreenWidth()/zoom_;
	double scaled_screen_height = GetScreenHeight()/zoom_;
	Vector2 max_distance_of_player_from_camera = Vector2(scaled_screen_width/2, scaled_screen_height/2)*camera_fraction_of_resolution_bound_;
	if(std::abs(player_position().x-camera_position_.x) > max_distance_of_player_from_camera.x)
		camera_position_.x = player_position().x + max_distance_of_player_from_camera.x*(player_position().x>camera_position_.x? -1 : 1);
	if(std::abs(player_position().y-camera_position_.y) > max_distance_of_player_from_camera.y)
		camera_position_.y = player_position().y + max_distance_of_player_from_camera.y*(player_position().y>camera_position_.y? -1 : 1);

	// ensure camera can not move outside of the map.
	if(camera_position_.x < scaled_screen_width/2) camera_position_.x = scaled_screen_width/2;
	if(camera_position_.y < scaled_screen_height/2) camera_position_.y = scaled_screen_height/2;
	int map_size_x = current_tile_set_.tile_size()*current_map_.size().x;
	int map_size_y = current_tile_set_.tile_size()*current_map_.size().y;
	if(camera_position_.x > map_size_x-scaled_screen_width/2)
		camera_position_.x = map_size_x-scaled_screen_width/2;
	if(camera_position_.y > map_size_y-scaled_screen_height/2)
		camera_position_.y = map_size_y-scaled_screen_height/2;
}

Vector2 Game::position_to_bucket_id(Vector2 position) const
{
	return Vector2(std::floor(position.x / bucket_size_), std::floor(position.y / bucket_size_));
}

std::unordered_set<Vector2> Game::get_bucket_ids(Rectangle rectangle) const
{
	std::unordered_set<Vector2> bucket_ids{};

	const auto top_left_bucket_id = position_to_bucket_id(Vector2(rectangle.x,rectangle.y));
	const auto bottom_right_bucket_id = position_to_bucket_id({rectangle.x+rectangle.width, rectangle.y+rectangle.height});
	for(int y = top_left_bucket_id.y; y <= bottom_right_bucket_id.y; ++y)
		for(int x = top_left_bucket_id.x; x <= bottom_right_bucket_id.x; ++x)
			bucket_ids.insert(Vector2(x,y));

	return bucket_ids;
}

std::unordered_set<Vector2> Game::get_entity_bucket_ids(std::size_t entity_id) const
{
	const auto& current_entity = entities_[entity_id];
	const auto& render_size = current_entity.entity->render_size();
	const Rectangle bounding_rectangle
	{
		.x=current_entity.position.x,
		.y=current_entity.position.y,
		.width=render_size.y,
		.height=render_size.x
	};
	return get_bucket_ids(bounding_rectangle);
}

void Game::insert_into_entity_bucket(std::size_t entity_id)
{
	const auto bucket_ids = get_entity_bucket_ids(entity_id);
	for(const auto &bucket_id : bucket_ids)
	{
		entity_buckets_[bucket_id].push_back(entity_id);
	}
}

void Game::update(std::chrono::nanoseconds del_time)
{
	entity_buckets_.clear();
	update_camera(del_time);
	
	for(int i{}; i<entities_.size(); ++i)
	{
		insert_into_entity_bucket(i);
	}

	for(auto& positioned_entity : entities_)
	{
		auto del_position = positioned_entity.entity->update(del_time, *this, positioned_entity.position);
		Vector2 new_unvalidated_position = positioned_entity.position+del_position;
		if(sprite_can_move_to_without_collision(new_unvalidated_position, positioned_entity.entity->render_size(), positioned_entity.position))
		{
			positioned_entity.position = new_unvalidated_position;
		}
	}
}

bool Game::position_is_free(const Vector2& position, const Vector2& sprite_size) const
{
	return (current_map_.is_reachable(position, current_tile_set_) &&
			current_map_.is_reachable(position+Vector2{sprite_size.x,0}, current_tile_set_) &&
			current_map_.is_reachable(position+sprite_size, current_tile_set_) &&
			current_map_.is_reachable(position+Vector2{0,sprite_size.y}, current_tile_set_));
}

Game::Game()
{
	entities_.emplace_back(std::make_unique<Player>(resource_buffer_), Vector2{80, 80});
	for(int y{}; y<10; ++y)
	for(int i{}; i<1000; ++i)
		entities_.emplace_back(std::make_unique<Enemy>(resource_buffer_), Vector2(100+i*5, 100+y*10));
}

Game::~Game()
{
	UnloadRenderTexture(render_texture_);
}

void Game::render()
{
	ClearBackground(WHITE);
	auto offset = Vector2(GetScreenWidth()/zoom_, GetScreenHeight()/zoom_)/2-camera_position_;
	current_map_.render(current_tile_set_, offset);
	for(const auto& positioned_entity : entities_)
	{
		positioned_entity.entity->render(positioned_entity.position+offset);
	}
	DrawFPS(0, 0);

}

void Game::display()
{
	BeginTextureMode(render_texture_);

		render();

	EndTextureMode();
	
	BeginDrawing();

		const auto source_rectangle = Rectangle(0, 0, render_texture_.texture.width,-render_texture_.texture.height);
		const auto destination_rectangle = Rectangle(0, 0, render_texture_.texture.width*zoom_, render_texture_.texture.height*zoom_);
		DrawTexturePro(render_texture_.texture, source_rectangle, destination_rectangle, Vector2(0,0), 0, WHITE);

	EndDrawing();
	
}

bool Game::sprite_can_move_to_without_collision(const Vector2& destination, const Vector2& sprite_size, const Vector2& starting_position)
{
	const auto step_size = std::min(sprite_size.x, sprite_size.y);
	for(int i{}; i<static_cast<int>(magnitude(destination-starting_position)/step_size); ++i)
	{
		if(!position_is_free(starting_position+i*(normalise(destination-starting_position)*step_size), sprite_size))
			return false;
	}

	if(!position_is_free(destination, sprite_size))
		return false;

	return true;
}
