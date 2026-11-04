// SceneLight (recomp/brick/scene_light.h) off the Brick: a frame and its 3D layer, as the Tekken 3 plugin dumps
// them on the PC build (DEADSTATION_TEKKEN3_DUMP: frame-N.ppm, frame-N.scene), drawn as Deadstation's presenter
// draws a page (its frame shaders, then the layer) into an image of the page's size, read back to a PPM.
//   scene_light_test FRAME.ppm FRAME.scene OUT.ppm [WIDTH HEIGHT]   (any Vulkan device: radv, lavapipe)
#include "scene_light.h"

#include "frame_frag.h"
#include "frame_vert.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace
{
constexpr uint32_t TexW = 1280, TexH = 1024;

uint32_t memoryType(vk::PhysicalDevice pd, uint32_t bits, vk::MemoryPropertyFlags flags)
{
	const auto props = pd.getMemoryProperties();
	for (uint32_t i = 0; i < props.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	throw std::runtime_error("no memory type");
}

bool readPpm(const char *path, std::vector<uint8_t>& rgb, int& w, int& h)
{
	std::ifstream f(path, std::ios::binary);
	std::string magic;
	int max;
	f >> magic >> w >> h >> max;
	f.get();
	rgb.resize((size_t)w * h * 3);
	f.read((char *)rgb.data(), (std::streamsize)rgb.size());
	return magic == "P6" && bool(f);
}

bool readScene(const char *path, deadstation::scene::Frame& out)
{
	std::ifstream f(path);
	std::string line;
	while (std::getline(f, line))
	{
		std::istringstream in(line);
		if (line.rfind("#", 0) == 0)
		{
			std::string hash, key;
			in >> hash >> key;
			if (key == "size")
				in >> out.width >> out.height;
			else if (key == "sun")
				in >> out.sun[0] >> out.sun[1] >> out.sun[2];
			else if (key == "ambient")
				in >> out.ambient;
			else if (key == "light")
			{
				deadstation::scene::Light l;
				if (in >> l.x >> l.y >> l.z >> l.r >> l.g >> l.b)
					out.lights.push_back(l);
			}
			else if (key == "floor")
			{
				deadstation::scene::Corner c;
				if (in >> c.sx >> c.sy >> c.x >> c.y >> c.z >> c.nx >> c.ny >> c.nz)
					out.floor.push_back(c);
			}
			continue;
		}
		deadstation::scene::Corner c;
		if (in >> c.sx >> c.sy >> c.x >> c.y >> c.z >> c.nx >> c.ny >> c.nz)
			out.corners.push_back(c);
	}
	return !out.corners.empty();
}
}

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		std::fprintf(stderr, "usage: scene_light_test FRAME.ppm FRAME.scene OUT.ppm [WIDTH HEIGHT]\n");
		return 2;
	}
	std::vector<uint8_t> rgb;
	int fw, fh;
	deadstation::scene::Frame scene;
	if (!readPpm(argv[1], rgb, fw, fh) || !readScene(argv[2], scene))
	{
		std::fprintf(stderr, "scene_light_test: can't read the frame or the scene\n");
		return 1;
	}
	const vk::Extent2D extent = { argc > 5 ? (uint32_t)atoi(argv[4]) : 1024u, argc > 5 ? (uint32_t)atoi(argv[5]) : 768u };

	static vk::detail::DynamicLoader loader;
	VULKAN_HPP_DEFAULT_DISPATCHER.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
	vk::ApplicationInfo app("scene_light_test", 1, "Deadstation", 1, VK_API_VERSION_1_1);
	vk::UniqueInstance instance = vk::createInstanceUnique(vk::InstanceCreateInfo(vk::InstanceCreateFlags(), &app));
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
	vk::PhysicalDevice pd = instance->enumeratePhysicalDevices().front();
	std::fprintf(stderr, "scene_light_test: %s\n", pd.getProperties().deviceName.data());
	const float priority = 1.f;
	vk::DeviceQueueCreateInfo qi(vk::DeviceQueueCreateFlags(), 0, 1, &priority);
	vk::UniqueDevice device = pd.createDeviceUnique(vk::DeviceCreateInfo(vk::DeviceCreateFlags(), qi));
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
	vk::Queue queue = device->getQueue(0, 0);
	auto image = [&](vk::Format format, vk::Extent2D e, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect, vk::UniqueImage& img,
			vk::UniqueDeviceMemory& mem, vk::UniqueImageView& view) {
		img = device->createImageUnique(vk::ImageCreateInfo(vk::ImageCreateFlags(), vk::ImageType::e2D, format, vk::Extent3D(e.width, e.height, 1),
				1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usage));
		const auto req = device->getImageMemoryRequirements(*img);
		mem = device->allocateMemoryUnique(vk::MemoryAllocateInfo(req.size, memoryType(pd, req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
		device->bindImageMemory(*img, *mem, 0);
		view = device->createImageViewUnique(vk::ImageViewCreateInfo(vk::ImageViewCreateFlags(), *img, vk::ImageViewType::e2D, format,
				vk::ComponentMapping(), vk::ImageSubresourceRange(aspect, 0, 1, 0, 1)));
	};
	auto buffer = [&](vk::DeviceSize size, vk::BufferUsageFlags usage, vk::UniqueBuffer& buf, vk::UniqueDeviceMemory& mem) {
		buf = device->createBufferUnique(vk::BufferCreateInfo(vk::BufferCreateFlags(), size, usage));
		const auto req = device->getBufferMemoryRequirements(*buf);
		mem = device->allocateMemoryUnique(vk::MemoryAllocateInfo(req.size, memoryType(pd, req.memoryTypeBits,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
		device->bindBufferMemory(*buf, *mem, 0);
		return device->mapMemory(*mem, 0, size);
	};

	// the page: colour (as Video's, B8G8R8A8) + depth (D16), the render pass as Video's when layered
	const vk::Format colorFormat = vk::Format::eB8G8R8A8Unorm, depthFormat = vk::Format::eD16Unorm;
	vk::UniqueImage color, depth, tex;
	vk::UniqueDeviceMemory colorMem, depthMem, texMem;
	vk::UniqueImageView colorView, depthView, texView;
	image(colorFormat, extent, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc, vk::ImageAspectFlagBits::eColor,
			color, colorMem, colorView);
	image(depthFormat, extent, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth, depth, depthMem, depthView);
	image(vk::Format::eB8G8R8A8Unorm, { TexW, TexH }, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::ImageAspectFlagBits::eColor, tex, texMem, texView);
	vk::AttachmentDescription attachments[2] = {
		{ vk::AttachmentDescriptionFlags(), colorFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal },
		{ vk::AttachmentDescriptionFlags(), depthFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal },
	};
	vk::AttachmentReference colorRef(0, vk::ImageLayout::eColorAttachmentOptimal), depthRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
	vk::SubpassDescription subpass(vk::SubpassDescriptionFlags(), vk::PipelineBindPoint::eGraphics, nullptr, colorRef, nullptr, &depthRef);
	vk::SubpassDependency dependency(0, vk::SubpassExternal, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
			vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead);
	vk::UniqueRenderPass renderPass = device->createRenderPassUnique(vk::RenderPassCreateInfo(vk::RenderPassCreateFlags(), attachments, subpass,
			dependency));
	vk::ImageView views[2] = { *colorView, *depthView };
	vk::UniqueFramebuffer framebuffer = device->createFramebufferUnique(vk::FramebufferCreateInfo(vk::FramebufferCreateFlags(), *renderPass,
			views, extent.width, extent.height, 1));

	// the frame: its texture, set and pipeline as Video's
	vk::DescriptorSetLayoutBinding binding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment);
	vk::UniqueDescriptorSetLayout setLayout = device->createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo(
			vk::DescriptorSetLayoutCreateFlags(), binding));
	struct FramePush { float uvOffset[2], uvScale[2], uvMin[2], uvMax[2]; };
	vk::PushConstantRange push(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(FramePush));
	vk::UniquePipelineLayout frameLayout = device->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(vk::PipelineLayoutCreateFlags(),
			*setLayout, push));
	vk::UniqueShaderModule fv = device->createShaderModuleUnique(vk::ShaderModuleCreateInfo(vk::ShaderModuleCreateFlags(), sizeof(frame_vert), frame_vert));
	vk::UniqueShaderModule ff = device->createShaderModuleUnique(vk::ShaderModuleCreateInfo(vk::ShaderModuleCreateFlags(), sizeof(frame_frag), frame_frag));
	vk::PipelineShaderStageCreateInfo stages[2] = { { {}, vk::ShaderStageFlagBits::eVertex, *fv, "main" },
		{ {}, vk::ShaderStageFlagBits::eFragment, *ff, "main" } };
	vk::PipelineVertexInputStateCreateInfo vin;
	vk::PipelineInputAssemblyStateCreateInfo ia({}, vk::PrimitiveTopology::eTriangleList);
	vk::Viewport viewport(0.f, 0.f, (float)extent.width, (float)extent.height, 0.f, 1.f);
	vk::Rect2D scissor({ 0, 0 }, extent);
	vk::PipelineViewportStateCreateInfo vs({}, viewport, scissor);
	vk::PipelineRasterizationStateCreateInfo rs({}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise,
			false, 0, 0, 0, 1);
	vk::PipelineMultisampleStateCreateInfo ms;
	vk::PipelineDepthStencilStateCreateInfo ds;
	vk::PipelineColorBlendAttachmentState cba;
	cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	vk::PipelineColorBlendStateCreateInfo cb({}, false, vk::LogicOp::eCopy, cba);
	vk::UniquePipeline framePipeline = device->createGraphicsPipelineUnique(nullptr, vk::GraphicsPipelineCreateInfo({}, stages, &vin, &ia, nullptr,
			&vs, &rs, &ms, &ds, &cb, nullptr, *frameLayout, *renderPass, 0)).value;
	vk::UniqueSampler sampler = device->createSamplerUnique(vk::SamplerCreateInfo({}, vk::Filter::eLinear, vk::Filter::eLinear,
			vk::SamplerMipmapMode::eNearest, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge,
			vk::SamplerAddressMode::eClampToEdge));
	vk::DescriptorPoolSize poolSize(vk::DescriptorType::eCombinedImageSampler, 1);
	vk::UniqueDescriptorPool pool = device->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo({}, 1, poolSize));
	vk::DescriptorSet frameSet = device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*pool, *setLayout)).front();
	vk::DescriptorImageInfo texInfo(*sampler, *texView, vk::ImageLayout::eShaderReadOnlyOptimal);
	device->updateDescriptorSets(vk::WriteDescriptorSet(frameSet, 0, 0, vk::DescriptorType::eCombinedImageSampler, texInfo), nullptr);

	// the layer
	SceneLight light;
	std::string error;
	if (!light.init({ pd, *device, *renderPass, *setLayout, extent, 1 }, error))
	{
		std::fprintf(stderr, "scene_light_test: %s\n", error.c_str());
		return 1;
	}
	deadstation::scene::publish(scene);

	vk::UniqueBuffer staging, readback;
	vk::UniqueDeviceMemory stagingMem, readbackMem;
	uint8_t *up = (uint8_t *)buffer((vk::DeviceSize)fw * fh * 4, vk::BufferUsageFlagBits::eTransferSrc, staging, stagingMem);
	for (int i = 0; i < fw * fh; i++)
		up[4 * i] = rgb[3 * i + 2], up[4 * i + 1] = rgb[3 * i + 1], up[4 * i + 2] = rgb[3 * i], up[4 * i + 3] = 255;
	const uint8_t *down = (const uint8_t *)buffer((vk::DeviceSize)extent.width * extent.height * 4, vk::BufferUsageFlagBits::eTransferDst,
			readback, readbackMem);

	vk::UniqueCommandPool cmdPool = device->createCommandPoolUnique(vk::CommandPoolCreateInfo({}, 0));
	vk::UniqueCommandBuffer cmdBuf = std::move(device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(*cmdPool,
			vk::CommandBufferLevel::ePrimary, 1)).front());
	vk::CommandBuffer cmd = *cmdBuf;
	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr,
			vk::ImageMemoryBarrier({}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *tex, range));
	cmd.copyBufferToImage(*staging, *tex, vk::ImageLayout::eTransferDstOptimal, vk::BufferImageCopy(0, (uint32_t)fw, (uint32_t)fh,
			vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), { 0, 0, 0 }, { (uint32_t)fw, (uint32_t)fh, 1 }));
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr,
			vk::ImageMemoryBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal,
					vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *tex, range));
	light.prepare(cmd, 0);
	const vk::ClearValue clears[2] = { vk::ClearColorValue(), vk::ClearDepthStencilValue(1.f, 0) };
	cmd.beginRenderPass(vk::RenderPassBeginInfo(*renderPass, *framebuffer, vk::Rect2D({ 0, 0 }, extent), clears), vk::SubpassContents::eInline);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *framePipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *frameLayout, 0, frameSet, nullptr);
	const FramePush fp = { { 0, 0 }, { (float)fw / TexW, (float)fh / TexH }, { 0, 0 }, { (fw - 0.5f) / TexW, (fh - 0.5f) / TexH } };
	cmd.pushConstants(*frameLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(fp), &fp);
	cmd.draw(3, 1, 0, 0);
	if (getenv("SCENE_LIGHT_OFF") == nullptr)
		light.draw(cmd, 0, frameSet, { { fp.uvScale[0], fp.uvScale[1] }, { fp.uvMax[0], fp.uvMax[1] }, { 0, 0 }, { 0, 0 } });
	cmd.endRenderPass();
	cmd.copyImageToBuffer(*color, vk::ImageLayout::eTransferSrcOptimal, *readback, vk::BufferImageCopy(0, extent.width, extent.height,
			vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), { 0, 0, 0 }, { extent.width, extent.height, 1 }));
	cmd.end();
	queue.submit(vk::SubmitInfo(nullptr, nullptr, cmd));
	queue.waitIdle();

	FILE *out = std::fopen(argv[3], "wb");
	if (out == nullptr)
		return 1;
	std::fprintf(out, "P6\n%u %u\n255\n", extent.width, extent.height);
	for (uint32_t i = 0; i < extent.width * extent.height; i++)
	{
		const uint8_t px[3] = { down[4 * i + 2], down[4 * i + 1], down[4 * i] };
		std::fwrite(px, 1, 3, out);
	}
	std::fclose(out);
	std::fprintf(stderr, "scene_light_test: %zu corners -> %s\n", scene.corners.size(), argv[3]);
	return 0;
}
