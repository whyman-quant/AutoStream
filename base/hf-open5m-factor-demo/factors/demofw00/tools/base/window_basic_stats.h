#pragma once

#include "factors/demofw00/tools/base/ring_buffer.hpp"
#include "factors/demofw00/tools/base/basic_stats.h" // Include for BasicStats
#include <deque>

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

struct MarkDataStats
{
    int mark;
    double value;
    size_t idx;
    MarkDataStats() {
        memset(this, 0, sizeof(MarkDataStats));
    }

    MarkDataStats(int _mark, double _value, size_t _idx): mark(_mark), value(_value), idx(_idx) {}
};

class WindowBasicStats {
public:
    WindowBasicStats(int window_size, int N=100000, int min_n=2, int skip_n=0) : N(N), min_n(min_n), skip_n(skip_n), m_queue_(N*2), m_rolling_stats_(min_n, skip_n), m_size_(1),
                                                                                        m_front_idx(0), m_back_idx(0), window_size(window_size){
        // std::cout << "WindowRollingStats: initialized with min_n=" << min_n << " skip_n=" << skip_n << std::endl;
    }

    void compute(){
        int curr_mark = m_queue_.back().mark;
        // std::cout << "WindowRollingStats: compute: curr_mark=" << curr_mark << std::endl;
        compute(curr_mark);
    }

    void compute(int int_mark) {
        if (update_flag){
            while(true) {
                // if (int(size()) >= N) {
                bool cond_event = int(size()) >= N;
                bool cond_mark = !m_queue_.empty() && m_queue_.front().mark <= int_mark - window_size;
                // std::cout << "    WindowRollingStats: compute: cond_event=" << cond_event << " cond_mark=" << cond_mark << std::endl;
                if (cond_event || cond_mark){
                    // verbose //////////////////////////////////////////////////////////////////////////////////
                    // int old_mark = m_queue_.front().mark;
                    // double old_x = m_queue_.front().value;
                    /////////////////////////////////////////////////////////////////////////////////////////////////

                    int last_pop_idx = -1;
                    int pop_count = 0;

                    // std::cout << "        m_front_idx: " << m_front_idx << " m_queue_.front().idx: " << m_queue_.front().idx << std::endl;
                    if(m_rolling_stats_.num() > 0) {
                        m_rolling_stats_.pop(m_queue_[m_queue_.front_index() + m_rolling_stats_.skip_n].value);
                        last_pop_idx = m_queue_.front_index() + m_rolling_stats_.skip_n;
                    }
                    // std::cout<< "        m_queue_.pop_front()" << std::endl;
                    m_queue_.pop_front();
                    ++pop_count;

                    if(last_pop_idx == -1) {
                        // std::cout<< "        last_pop_idx == -1" << std::endl;
                        m_rolling_stats_.add_skip(pop_count);
                    } else {
                        int pop_count = last_pop_idx - m_queue_.front_index() + 1;
                        // std::cout<< "        pop_count == " << pop_count << std::endl;
                        if(pop_count < m_rolling_stats_.skip_n) {
                            m_rolling_stats_.set_skip(m_rolling_stats_.skip_n - pop_count);
                        }
                    }

                    ++m_front_idx;
                    --m_size_;

                    // verbose //////////////////////////////////////////////////////////////////////////////////
                    // int next_mark = m_queue_.front().mark;
                    // std::cout<< "    popped old_mark=" << old_mark <<
                    //             ", old_x=" << old_x <<
                    //             ", on int_mark=" << int_mark <<
                    //             ", next_mark=" << next_mark <<
                    //             ", m_queue_.size()=" << m_queue_.size() << std::endl;
                    /////////////////////////////////////////////////////////////////////////////////////////////////
                } else {
                    break;
                    // std::cout << "    WindowRollingStats: not yet poping out, " << m_queue_.size() << std::endl;
                }
            }
            update_flag = false;
            // std::cout<< "    compute updated, m_rolling_stats_.num()=" << m_rolling_stats_.num() << std::endl;
        } else {
            // std::cout << "    WindowRollingStats: not updated" << std::endl;
        }
    }

    // 更新隊列中的所有 RunningStats 對象
    void update(int int_mark, double x) {
        // std::cout << "WindowRollingStats: update: " << x << " update_flag=" << update_flag << std::endl;
        m_rolling_stats_.update(x);
        m_queue_.push_back(MarkDataStats(int_mark, x, m_back_idx));

        // 隊列最後面位置
        ++m_size_;
        ++m_back_idx;
        update_flag = true;
    }


    // 獲取隊列的大小
    size_t size() const {
        return m_size_;
    }

    // 返回隊列前端的統計量函數
    long long num() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.num();
    }

    double mean() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();;
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.mean();
    }

    double variance() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();;

        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.variance();
    }

    double std_dev() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();;

        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.std_dev();
    }

    double skew() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();;

        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.skew();
    }

    double std() const {
        return std_dev();
    }

    double kurt() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();;

        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.kurt();
    }

    double jarque_bera() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();;

        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.jarque_bera();
    }

    double bimodality() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();;

        if (size() < min_n) return 0;
        return m_rolling_stats_.bimodality();
    }

    double skew_kurt_ratio() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();;

        if (size() < min_n) return 0;
        return m_rolling_stats_.skew_kurt_ratio();
    }

    // 透出 BasicStats 的新增指标
    double sum_squares() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.sum_squares();
    }

    double rms() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.rms();
    }

    double cv() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.cv();
    }

    double se() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.se();
    }

    double mean_abs() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.mean_abs();
    }

    double geometric_mean() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.geometric_mean();
    }

    double harmonic_mean() const {
        if (update_flag) const_cast<WindowBasicStats*>(this)->compute();
        if (size() < static_cast<size_t>(min_n)) return 0;
        return m_rolling_stats_.harmonic_mean();
    }


    int N;
    int min_n;
    int skip_n;
    bool update_flag = false;
private:
    RingBuffer<MarkDataStats> m_queue_;
    BasicStats m_rolling_stats_;
    size_t m_size_; // 隊列大小
    size_t m_front_idx; // 隊列最前面位置
    size_t m_back_idx; // 隊列最後面位置
    int window_size;

};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
