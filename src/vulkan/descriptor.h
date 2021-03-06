// Copyright 2018 The Amber Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_VULKAN_DESCRIPTOR_H_
#define SRC_VULKAN_DESCRIPTOR_H_

#include <memory>
#include <vector>

#include "amber/result.h"
#include "amber/vulkan_header.h"
#include "src/datum_type.h"
#include "src/engine.h"
#include "src/vulkan/resource.h"

namespace amber {
namespace vulkan {

enum class DescriptorType : uint8_t {
  kStorageImage = 0,
  kSampler,
  kSampledImage,
  kCombinedImageSampler,
  kUniformTexelBuffer,
  kStorageTexelBuffer,
  kStorageBuffer,
  kUniformBuffer,
  kDynamicUniformBuffer,
  kDynamicStorageBuffer,
  kInputAttachment,
};

class CommandBuffer;
class Device;

VkDescriptorType ToVkDescriptorType(DescriptorType type);

class Descriptor {
 public:
  Descriptor(DescriptorType type,
             Device* device,
             uint32_t desc_set,
             uint32_t binding);

  virtual ~Descriptor();

  uint32_t GetDescriptorSet() const { return descriptor_set_; }
  uint32_t GetBinding() const { return binding_; }

  DescriptorType GetType() const { return type_; }

  bool IsStorageImage() const { return type_ == DescriptorType::kStorageImage; }
  bool IsSampler() const { return type_ == DescriptorType::kSampler; }
  bool IsSampledImage() const { return type_ == DescriptorType::kSampledImage; }
  bool IsCombinedImageSampler() const {
    return type_ == DescriptorType::kCombinedImageSampler;
  }
  bool IsUniformTexelBuffer() const {
    return type_ == DescriptorType::kUniformTexelBuffer;
  }
  bool IsStorageTexelBuffer() const {
    return type_ == DescriptorType::kStorageTexelBuffer;
  }
  bool IsStorageBuffer() const {
    return type_ == DescriptorType::kStorageBuffer;
  }
  bool IsUniformBuffer() const {
    return type_ == DescriptorType::kUniformBuffer;
  }
  bool IsDynamicUniformBuffer() const {
    return type_ == DescriptorType::kDynamicUniformBuffer;
  }
  bool IsDynamicStorageBuffer() const {
    return type_ == DescriptorType::kDynamicStorageBuffer;
  }

  bool HasDataNotSent() { return !buffer_input_queue_.empty(); }

  // Add the information to |buffer_input_queue_| that "we will fill
  // resource of this descriptor with |values| at |offset| of the
  // resource". |type| indicates the primitive type of |values| and
  // |size_in_bytes| denotes the total size in bytes of |values|.
  void AddToBufferInputQueue(DataType type,
                             uint32_t offset,
                             size_t size_in_bytes,
                             const std::vector<Value>& values);

  // Call vkUpdateDescriptorSets() to update the backing resource
  // for this descriptor only when the backing resource was newly
  // created or changed.
  virtual Result UpdateDescriptorSetIfNeeded(VkDescriptorSet) = 0;

  // Create vulkan resource e.g., buffer or image used for this
  // descriptor if |buffer_input_queue_| is not empty. This method
  // assumes that the resource is empty when it is called that means
  // the resource must be created only when it is actually needed
  // i.e., compute or draw command and destroyed right after those
  // commands.
  virtual Result CreateResourceIfNeeded(
      const VkPhysicalDeviceMemoryProperties& properties) = 0;

  // Record a command for copying |buffer_output_| and
  // |buffer_input_queue_| to the resource in device. After the copy
  // it clears |buffer_output_| and |buffer_input_queue_|. Note that
  // it only records the command and the actual submission must be
  // done later.
  virtual Result RecordCopyDataToResourceIfNeeded(CommandBuffer* command) = 0;

  // Only record the copy command for copying the resource data to
  // the host accessible memory. The actual submission of the command
  // must be done later.
  virtual Result RecordCopyDataToHost(CommandBuffer* command) = 0;

  // Copy contents of resource e.g., VkBuffer to host buffer
  // |buffer_output_|. This method assumes that we already copy
  // the resource data to the host accessible memory by calling
  // RecordCopyDataToHost() method and submitting the command buffer.
  // After copying the contents, it destroys |buffer_|.
  virtual Result MoveResourceToBufferOutput() = 0;

  // If the resource is empty and |buffer_input_queue_| has a single
  // BufferInput, the BufferInput is the data copied from the resource
  // before. In that case, this methods returns information for the
  // BufferInput. If the resource is not empty, it returns
  // |size_in_bytes_| and host accessible memory of the resource.
  // Otherwise, it returns nullptr for |cpu_memory| of ResourceInfo.
  virtual ResourceInfo GetResourceInfo() = 0;

  virtual void Shutdown() = 0;

 protected:
  Result UpdateDescriptorSetForBuffer(
      VkDescriptorSet descriptor_set,
      VkDescriptorType descriptor_type,
      const VkDescriptorBufferInfo& buffer_info);
  Result UpdateDescriptorSetForImage(VkDescriptorSet descriptor_set,
                                     VkDescriptorType descriptor_type,
                                     const VkDescriptorImageInfo& image_info);
  Result UpdateDescriptorSetForBufferView(VkDescriptorSet descriptor_set,
                                          VkDescriptorType descriptor_type,
                                          const VkBufferView& texel_view);

  std::vector<BufferInput>& GetBufferInputQueue() {
    return buffer_input_queue_;
  }
  std::vector<uint8_t>& GetBufferOutput() { return buffer_output_; }

  void ClearBufferInputQueue() { buffer_input_queue_.clear(); }
  void ClearBufferOutput() { buffer_output_.clear(); }

  void SetUpdateDescriptorSetNeeded() {
    is_descriptor_set_update_needed_ = true;
  }
  bool IsDescriptorSetUpdateNeeded() {
    return is_descriptor_set_update_needed_;
  }

  uint32_t descriptor_set_ = 0;
  uint32_t binding_ = 0;
  Device* device_ = nullptr;

 private:
  VkWriteDescriptorSet GetWriteDescriptorSet(
      VkDescriptorSet descriptor_set,
      VkDescriptorType descriptor_type) const;
  void UpdateVkDescriptorSet(const VkWriteDescriptorSet& write);

  DescriptorType type_ = DescriptorType::kSampledImage;

  // Each element of this queue contains information of what parts
  // of buffer must be updates with what values. This queue will be
  // consumed and cleared by CopyDataToResourceIfNeeded().
  // CopyDataToResourceIfNeeded() updates the actual buffer in device
  // using this queued information.
  std::vector<BufferInput> buffer_input_queue_;

  // Vector to keep data from GPU memory i.e., read back from VkBuffer.
  std::vector<uint8_t> buffer_output_;

  bool is_descriptor_set_update_needed_ = false;
};

}  // namespace vulkan
}  // namespace amber

#endif  // SRC_VULKAN_DESCRIPTOR_H_
