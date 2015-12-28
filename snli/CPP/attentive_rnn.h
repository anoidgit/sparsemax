#ifndef RNN_H_
#define RNN_H_

#include <vector>
#include <cstdlib>
#include <Eigen/Dense>
#include <cmath>
#include "utils.h"
#include "nn_utils.h"
#include "snli_data.h"

class LookupLayer {
 public:
  LookupLayer(Dictionary *dictionary, int embedding_dimension) :
      dictionary_(dictionary) {
    embedding_dimension_ = embedding_dimension;
  }

  virtual ~LookupLayer() {}

  int embedding_dimension() { return embedding_dimension_; }

  void CollectAllParameters(std::vector<Matrix*> *weights,
                            std::vector<Vector*> *biases,
                            std::vector<std::string> *weight_names,
                            std::vector<std::string> *bias_names) {
    E_ = Matrix::Zero(embedding_dimension_, dictionary_->GetNumWords()+1);

    weights->clear();
    biases->clear();
    weight_names->clear();
    bias_names->clear();

    weights->push_back(&E_);
    weight_names->push_back("embeddings");
  }

  void RunForwardLookupLayer(const std::vector<Input> &input_sequence,
                             Matrix *x) {
    x->setZero(embedding_dimension_, input_sequence.size());
    for (int t = 0; t < input_sequence.size(); ++t) {
      int wid = 1+input_sequence[t].wid(); // Check if should add 1.
      assert(wid >= 0 && wid < E_.cols());
      x->col(t) = E_.col(wid);
    }
  }

  void RunBackwardLookupLayer(const std::vector<Input> &input_sequence,
                              const Matrix &dX,
                              double learning_rate) {
    // Update word embeddings.
    for (int t = 0; t < input_sequence.size(); ++t) {
      int wid = 1+input_sequence[t].wid(); // Check if should add 1.
      E_.col(wid) -= learning_rate * dX.col(t);
    }
  }

 public:
  int embedding_dimension_;
  Dictionary *dictionary_;
  Matrix E_;
};

class RNN {
 public:
  RNN() {}
  RNN(Dictionary *dictionary,
      int embedding_dimension,
      int hidden_size,
      int output_size) : dictionary_(dictionary) {
    activation_function_ = ActivationFunctions::LOGISTIC;
    lookup_layer_ = new LookupLayer(dictionary, embedding_dimension);
    hidden_size_ = hidden_size;
    output_size_ = output_size;
    use_hidden_start_ = true;
  }
  virtual ~RNN() { delete lookup_layer_; }

  int GetInputSize() { return lookup_layer_->embedding_dimension(); }

  virtual void CollectAllParameters(std::vector<Matrix*> *weights,
                                    std::vector<Vector*> *biases,
                                    std::vector<std::string> *weight_names,
                                    std::vector<std::string> *bias_names) {
    Wxh_ = Matrix::Zero(hidden_size_, GetInputSize());
    Whh_ = Matrix::Zero(hidden_size_, hidden_size_);
    Why_ = Matrix::Zero(output_size_, hidden_size_);
    bh_ = Vector::Zero(hidden_size_);
    by_ = Vector::Zero(output_size_);
    if (use_hidden_start_) {
      h0_ = Vector::Zero(hidden_size_);
    }

    weights->clear();
    biases->clear();
    weight_names->clear();
    bias_names->clear();

    lookup_layer_->CollectAllParameters(weights, biases, weight_names,
                                        bias_names);

    weights->push_back(&Wxh_);
    weights->push_back(&Whh_);
    weights->push_back(&Why_);

    biases->push_back(&bh_);
    biases->push_back(&by_);
    if (use_hidden_start_) {
      biases->push_back(&h0_); // Not really a bias, but it goes here.
    }

    weight_names->push_back("Wxh");
    weight_names->push_back("Whh");
    weight_names->push_back("Why");

    bias_names->push_back("bh");
    bias_names->push_back("by");
    if (use_hidden_start_) {
      bias_names->push_back("h0");
    }
  }

  void InitializeParameters() {
    srand(1234);
    std::vector<Matrix*> weights;
    std::vector<Vector*> biases;
    std::vector<std::string> weight_names;
    std::vector<std::string> bias_names;
    CollectAllParameters(&weights, &biases, &weight_names, &bias_names);

    bool read_from_file = false;
    if (read_from_file) {
      for (int i = 0; i < biases.size(); ++i) {
        auto b = biases[i];
        auto name = bias_names[i];
        std::cout << "Loading " << name << "..." << std::endl;
        LoadVectorParameter(name, b);
      }
      for (int i = 0; i < weights.size(); ++i) {
        auto W = weights[i];
        auto name = weight_names[i];
        std::cout << "Loading " << name << "..." << std::endl;
        LoadMatrixParameter(name, W);
      }
      return;
    }

    for (auto b: biases) {
      b->setZero();
    }
    for (auto W: weights) {
      int num_outputs = W->rows();
      int num_inputs = W->cols();
      double coeff;
      if (activation_function_ == ActivationFunctions::LOGISTIC) {
        coeff = 4.0;
      } else {
        coeff = 1.0;
      }
      double max = coeff * sqrt(6.0 / (num_inputs + num_outputs));
      for (int i = 0; i < W->rows(); ++i) {
        for (int j = 0; j < W->cols(); ++j) {
          double t = max *
            (2.0*static_cast<double>(rand()) / RAND_MAX - 1.0);
          (*W)(i, j) = t;
          //std::cout << t/max << std::endl;
        }
      }
    }
  }

  void Train(const std::vector<std::vector<Input> > &input_sequences,
             const std::vector<int> &output_labels,
             const std::vector<std::vector<Input> > &input_sequences_dev,
             const std::vector<int> &output_labels_dev,
             const std::vector<std::vector<Input> > &input_sequences_test,
             const std::vector<int> &output_labels_test,
             int num_epochs,
             double learning_rate) {

    // Initial performance.
    double accuracy_dev = 0.0;
    int num_sentences_dev = input_sequences_dev.size();
    for (int i = 0; i < input_sequences_dev.size(); ++i) {
      int predicted_label;
      Run(input_sequences_dev[i], &predicted_label);
      if (output_labels_dev[i] == predicted_label) {
        accuracy_dev += 1.0;
      }
    }
    accuracy_dev /= num_sentences_dev;
    std::cout << " Initial accuracy dev: " << accuracy_dev
              << std::endl;

    for (int epoch = 0; epoch < num_epochs; ++epoch) {
      TrainEpoch(input_sequences, output_labels,
                 input_sequences_dev, output_labels_dev,
                 input_sequences_test, output_labels_test,
                 epoch, learning_rate);
    }
  }

  void TrainEpoch(const std::vector<std::vector<Input> > &input_sequences,
                  const std::vector<int> &output_labels,
                  const std::vector<std::vector<Input> > &input_sequences_dev,
                  const std::vector<int> &output_labels_dev,
                  const std::vector<std::vector<Input> > &input_sequences_test,
                  const std::vector<int> &output_labels_test,
                  int epoch,
                  double learning_rate) {
    timeval start, end;
    gettimeofday(&start, NULL);
    double total_loss = 0.0;
    double accuracy = 0.0;
    int num_sentences = input_sequences.size();
    for (int i = 0; i < input_sequences.size(); ++i) {
      RunForwardPass(input_sequences[i]);
      double loss = -log(p_(output_labels[i]));
      int prediction;
      p_.maxCoeff(&prediction);
      if (prediction == output_labels[i]) {
        accuracy += 1.0;
      }
      total_loss += loss;
      RunBackwardPass(input_sequences[i], output_labels[i], learning_rate);
    }
    accuracy /= num_sentences;

    double accuracy_dev = 0.0;
    int num_sentences_dev = input_sequences_dev.size();
    for (int i = 0; i < input_sequences_dev.size(); ++i) {
      int predicted_label;
      Run(input_sequences_dev[i], &predicted_label);
      if (output_labels_dev[i] == predicted_label) {
        accuracy_dev += 1.0;
      }
    }
    accuracy_dev /= num_sentences_dev;

    double accuracy_test = 0.0;
    int num_sentences_test = input_sequences_test.size();
    for (int i = 0; i < input_sequences_test.size(); ++i) {
      int predicted_label;
      Run(input_sequences_test[i], &predicted_label);
      if (output_labels_test[i] == predicted_label) {
        accuracy_test += 1.0;
      }
    }
    accuracy_test /= num_sentences_test;

    gettimeofday(&end, NULL);
    std::cout << "Epoch: " << epoch+1
              << " Total loss: " << total_loss
              << " Accuracy train: " << accuracy
              << " Accuracy dev: " << accuracy_dev
              << " Accuracy test: " << accuracy_test
              << " Time: " << diff_ms(end,start)
              << std::endl;
  }

  void Run(const std::vector<Input> &input_sequence,
           int *predicted_label) {
    RunForwardPass(input_sequence);
    int prediction;
    p_.maxCoeff(&prediction);
    *predicted_label = prediction;
  }

  virtual void RunForwardPass(const std::vector<Input> &input_sequence) {
    lookup_layer_->RunForwardLookupLayer(input_sequence, &x_);

    int hidden_size = Whh_.rows();
    int output_size = Why_.rows();

    h_.setZero(hidden_size, input_sequence.size());
    Vector hprev = Vector::Zero(h_.rows());
    if (use_hidden_start_) hprev = h0_;
    for (int t = 0; t < input_sequence.size(); ++t) {
      Matrix result;
      EvaluateActivation(activation_function_,
                         Wxh_ * x_.col(t) + bh_ + Whh_ * hprev,
                         &result);
      h_.col(t) = result;
      hprev = h_.col(t);
    }

    int t = input_sequence.size() - 1;
    y_ = Why_ * h_.col(t) + by_;
    double logsum = LogSumExp(y_);
    p_ = (y_.array() - logsum).exp();
  }

  virtual void RunBackwardPass(const std::vector<Input> &input_sequence,
                               int output_label,
                               double learning_rate) {
    Matrix dWhy = Matrix::Zero(Why_.rows(), Why_.cols());
    Matrix dWhh = Matrix::Zero(Whh_.rows(), Whh_.cols());
    Matrix dWxh = Matrix::Zero(Wxh_.rows(), Wxh_.cols());
    Vector dby = Vector::Zero(Why_.rows());
    Vector dbh = Vector::Zero(Whh_.rows());
    Vector dhnext = Vector::Zero(Whh_.rows());
    Matrix dx = Matrix::Zero(GetInputSize(), input_sequence.size());

    Vector dy = p_;
    int t = input_sequence.size() - 1;
    dy[output_label] -= 1.0; // Backprop into y (softmax grad).
    dWhy += dy * h_.col(t).transpose();
    dby += dy;
    dhnext += Why_.transpose() * dy; // Backprop into h.

    for (int t = input_sequence.size() - 1; t >= 0; --t) {
      //Vector dh = Why_.transpose() * dy + dhnext; // Backprop into h.
      Vector dh = dhnext; // Backprop into h.
      Matrix dhraw;
      DerivateActivation(activation_function_, h_.col(t), &dhraw);
      dhraw = dhraw.array() * dh.array();

      dWxh += dhraw * x_.col(t).transpose();
      dbh += dhraw;
      if (t > 0) {
        dWhh += dhraw * h_.col(t-1).transpose();
      }
      dhnext.noalias() = Whh_.transpose() * dhraw;

      dx.col(t) = Wxh_.transpose() * dhraw; // Backprop into x.
    }

    Why_ -= learning_rate * dWhy;
    by_ -= learning_rate * dby;
    Wxh_ -= learning_rate * dWxh;
    bh_ -= learning_rate * dbh;
    Whh_ -= learning_rate * dWhh;

    if (use_hidden_start_) {
      h0_ -= learning_rate * dhnext;
    }

    lookup_layer_->RunBackwardLookupLayer(input_sequence, dx, learning_rate);
  }

 protected:
  Dictionary *dictionary_;
  int activation_function_;
  LookupLayer *lookup_layer_;
  int hidden_size_;
  int output_size_;
  bool use_hidden_start_;

  Matrix Wxh_;
  Matrix Whh_;
  Matrix Why_;
  Vector bh_;
  Vector by_;
  Vector h0_;

  Matrix x_;
  Matrix h_;
  Vector y_;
  Vector p_;
};

#if 0
class BiRNN : public RNN {
 public:
  BiRNN(Dictionary *dictionary,
        int window_size, int embedding_dimension, int affix_embedding_dimension,
        int hidden_size, int output_size) {
    activation_function_ = ActivationFunctions::LOGISTIC;
    dictionary_ = dictionary;
    window_size_ = window_size;
    embedding_dimension_ = embedding_dimension;
    affix_embedding_dimension_ = affix_embedding_dimension;
    hidden_size_ = hidden_size;
    output_size_ = output_size;
    use_hidden_start_ = true;
  }
  ~BiRNN() {}

  void CollectAllParameters(std::vector<Matrix*> *weights,
                            std::vector<Vector*> *biases,
                            std::vector<std::string> *weight_names,
                            std::vector<std::string> *bias_names) {
    RNN::CollectAllParameters(weights, biases, weight_names, bias_names);

    Wxl_ = Matrix::Zero(hidden_size_, GetInputSize());
    Wll_ = Matrix::Zero(hidden_size_, hidden_size_);
    Wly_ = Matrix::Zero(output_size_, hidden_size_);
    bl_ = Vector::Zero(hidden_size_, 1);
    if (use_hidden_start_) {
      l0_ = Vector::Zero(hidden_size_);
    }

    weights->push_back(&Wxl_);
    weights->push_back(&Wll_);
    weights->push_back(&Wly_);

    biases->push_back(&bl_);
    if (use_hidden_start_) {
      biases->push_back(&l0_); // Not really a bias, but it goes here.
    }

    weight_names->push_back("Wxl");
    weight_names->push_back("Wll");
    weight_names->push_back("Wly");

    bias_names->push_back("bl");
    if (use_hidden_start_) {
      bias_names->push_back("l0");
    }
  }

  void RunForwardPass(const std::vector<Input> &input_sequence) {
    RunForwardLookupLayer(input_sequence);

    int hidden_size = Whh_.rows();
    int output_size = Why_.rows();

    h_.setZero(hidden_size, input_sequence.size());
    Vector hprev = Vector::Zero(h_.rows());
    if (use_hidden_start_) hprev = h0_;
    for (int t = 0; t < input_sequence.size(); ++t) {
      Matrix result;
      EvaluateActivation(activation_function_,
                         Wxh_ * x_.col(t) + bh_ + Whh_ * hprev,
                         &result);
      h_.col(t) = result;

#if 0
      //std::cout << "x[" << t << "] = " <<  x_.col(t).transpose() << std::endl;
      //std::cout << "Wxh*x[" << t << "] = " <<  (Wxh_ * x_.col(t)).transpose() << std::endl;
      //std::cout << "Whh*hprev[" << t << "] = " <<  (Whh_ * hprev).transpose() << std::endl;
      //std::cout << "bh = " <<  bh_.transpose() << std::endl;
      std::cout << "h[" << t << "] = " << h_.col(t).transpose() << std::endl;
#endif

      hprev = h_.col(t);
    }

    l_.setZero(hidden_size, input_sequence.size());
    Vector lnext = Vector::Zero(l_.rows());
    if (use_hidden_start_) lnext = l0_;
    for (int t = input_sequence.size() - 1; t >= 0; --t) {
      Matrix result;
      EvaluateActivation(activation_function_,
                         Wxl_ * x_.col(t) + bl_ + Wll_ * lnext,
                         &result);
      l_.col(t) = result;

#if 0
      std::cout << "l[" << t << "] = " << l_.col(t).transpose() << std::endl;
#endif

      lnext = l_.col(t);
    }

    y_.setZero(output_size, input_sequence.size());
    p_.setZero(output_size, input_sequence.size());
    for (int t = 0; t < input_sequence.size(); ++t) {
      y_.col(t) = Why_ * h_.col(t) + Wly_ * l_.col(t) + by_;
      double logsum = LogSumExp(y_.col(t));
      p_.col(t) = (y_.col(t).array() - logsum).exp();

#if 0
      std::cout << "p[" << t << "] = " << p_.col(t).transpose() << std::endl;
#endif

    }
  }

  void RunBackwardPass(const std::vector<Input> &input_sequence,
                       const std::vector<int> &output_sequence,
                       double learning_rate) {
    Matrix dWhy = Matrix::Zero(Why_.rows(), Why_.cols());
    Matrix dWhh = Matrix::Zero(Whh_.rows(), Whh_.cols());
    Matrix dWxh = Matrix::Zero(Wxh_.rows(), Wxh_.cols());
    Vector dby = Vector::Zero(Why_.rows());
    Vector dbh = Vector::Zero(Whh_.rows());
    Vector dhnext = Vector::Zero(Whh_.rows());

    Matrix dWly = Matrix::Zero(Wly_.rows(), Wly_.cols());
    Matrix dWll = Matrix::Zero(Wll_.rows(), Wll_.cols());
    Matrix dWxl = Matrix::Zero(Wxl_.rows(), Wxl_.cols());
    Vector dbl = Vector::Zero(Wll_.rows());
    Vector dlprev = Vector::Zero(Wll_.rows());

    Matrix dx = Matrix::Zero(GetInputSize(), input_sequence.size());

    for (int t = 0; t < input_sequence.size(); ++t) {
      Vector dy = p_.col(t);
      dy[output_sequence[t]] -= 1.0; // Backprop into y (softmax grad).

      dWly += dy * l_.col(t).transpose();

      Vector dl = Wly_.transpose() * dy + dlprev; // Backprop into l.
      Matrix dlraw;
      DerivateActivation(activation_function_, l_.col(t), &dlraw);
      dlraw = dlraw.array() * dl.array();

      dWxl += dlraw * x_.col(t).transpose();
      dbl += dlraw;
      if (t < input_sequence.size() - 1) {
        dWll += dlraw * l_.col(t+1).transpose();
      }
      dlprev.noalias() = Wll_.transpose() * dlraw;

      dx.col(t) = Wxl_.transpose() * dlraw; // Backprop into x.
    }

    for (int t = input_sequence.size() - 1; t >= 0; --t) {
      Vector dy = p_.col(t);
      dy[output_sequence[t]] -= 1.0; // Backprop into y (softmax grad).

      dWhy += dy * h_.col(t).transpose();
      dby += dy;

      Vector dh = Why_.transpose() * dy + dhnext; // Backprop into h.
      Matrix dhraw;
      DerivateActivation(activation_function_, h_.col(t), &dhraw);
      dhraw = dhraw.array() * dh.array();

      dWxh += dhraw * x_.col(t).transpose();
      dbh += dhraw;
      if (t > 0) {
        dWhh += dhraw * h_.col(t-1).transpose();
      }
      dhnext.noalias() = Whh_.transpose() * dhraw;

      dx.col(t) += Wxh_.transpose() * dhraw; // Backprop into x.
    }

    Why_ -= learning_rate * dWhy;
    by_ -= learning_rate * dby;
    Wxh_ -= learning_rate * dWxh;
    bh_ -= learning_rate * dbh;
    Whh_ -= learning_rate * dWhh;
    if (use_hidden_start_) {
      h0_ -= learning_rate * dhnext;
    }

    Wly_ -= learning_rate * dWly;
    Wxl_ -= learning_rate * dWxl;
    bl_ -= learning_rate * dbl;
    Wll_ -= learning_rate * dWll;
    if (use_hidden_start_) {
      l0_ -= learning_rate * dlprev;
    }

    RunBackwardLookupLayer(input_sequence, dx, learning_rate);
  }

 protected:
  Matrix Wxl_;
  Matrix Wll_;
  Matrix Wly_;
  Vector bl_;
  Vector l0_;

  Matrix l_;
};


class RNN_GRU : public RNN {
 public:
  RNN_GRU() {}
  RNN_GRU(Dictionary *dictionary,
          int window_size, int embedding_dimension, int affix_embedding_dimension,
          int hidden_size, int output_size) {
    dictionary_ = dictionary;
    activation_function_ = ActivationFunctions::LOGISTIC;
    window_size_ = window_size;
    embedding_dimension_ = embedding_dimension;
    affix_embedding_dimension_ = affix_embedding_dimension;
    hidden_size_ = hidden_size;
    output_size_ = output_size;
    use_hidden_start_ = true;
  }
  virtual ~RNN_GRU() {}

  virtual void CollectAllParameters(std::vector<Matrix*> *weights,
                                    std::vector<Vector*> *biases,
                                    std::vector<std::string> *weight_names,
                                    std::vector<std::string> *bias_names) {
    RNN::CollectAllParameters(weights, biases, weight_names, bias_names);

    Wxz_ = Matrix::Zero(hidden_size_, GetInputSize());
    Whz_ = Matrix::Zero(hidden_size_, hidden_size_);
    Wxr_ = Matrix::Zero(hidden_size_, GetInputSize());
    Whr_ = Matrix::Zero(hidden_size_, hidden_size_);
    bz_ = Vector::Zero(hidden_size_, 1);
    br_ = Vector::Zero(hidden_size_, 1);

    weights->push_back(&Wxz_);
    weights->push_back(&Whz_);
    weights->push_back(&Wxr_);
    weights->push_back(&Whr_);

    biases->push_back(&bz_);
    biases->push_back(&br_);

    weight_names->push_back("Wxz");
    weight_names->push_back("Whz");
    weight_names->push_back("Wxr");
    weight_names->push_back("Whr");

    bias_names->push_back("bz");
    bias_names->push_back("br");
  }

  virtual void RunForwardPass(const std::vector<Input> &input_sequence) {
    RunForwardLookupLayer(input_sequence);

    int hidden_size = Whh_.rows();
    int output_size = Why_.rows();

    z_.setZero(hidden_size, input_sequence.size());
    r_.setZero(hidden_size, input_sequence.size());
    hu_.setZero(hidden_size, input_sequence.size());
    h_.setZero(hidden_size, input_sequence.size());
    Vector hprev = Vector::Zero(h_.rows());
    if (use_hidden_start_) hprev = h0_;
    for (int t = 0; t < input_sequence.size(); ++t) {
      Matrix result;
      EvaluateActivation(ActivationFunctions::LOGISTIC,
                         Wxz_ * x_.col(t) + bz_ + Whz_ * hprev,
                         &result);
      z_.col(t) = result;

      EvaluateActivation(ActivationFunctions::LOGISTIC,
                         Wxr_ * x_.col(t) + br_ + Whr_ * hprev,
                         &result);
      r_.col(t) = result;

      EvaluateActivation(activation_function_,
                         Wxh_ * x_.col(t) + bh_ + Whh_ * r_.col(t).cwiseProduct(hprev),
                         &result);
      hu_.col(t) = result;

      //h_.col(t) = z_.col(t) * hu_.col(t) + (1.0 - z_.col(t)) * hprev;
      h_.col(t) = z_.col(t).cwiseProduct(hu_.col(t) - hprev) + hprev;

      //Vector hraw = Wxh_ * x_.col(t) + bh_;
      //if (t > 0) hraw += Whh_ * h_.col(t-1);
      //Matrix result;
      //EvaluateActivation(activation_function_, hraw, &result);
      //h_.col(t) = result;

      hprev = h_.col(t);
    }

    y_.setZero(output_size, input_sequence.size());
    p_.setZero(output_size, input_sequence.size());
    //Matrix y = (Why_ * h).colwise() + by_;
    //Vector logsums = LogSumExpColumnwise(y);
    //Matrix p = (y.rowwise() - logsums.transpose()).array().exp();
    for (int t = 0; t < input_sequence.size(); ++t) {
      y_.col(t) = Why_ * h_.col(t) + by_;
      double logsum = LogSumExp(y_.col(t));
      p_.col(t) = (y_.col(t).array() - logsum).exp();
    }
  }

  virtual void RunBackwardPass(const std::vector<Input> &input_sequence,
                       const std::vector<int> &output_sequence,
                       double learning_rate) {
    Matrix dWhy = Matrix::Zero(Why_.rows(), Why_.cols());
    Matrix dWhh = Matrix::Zero(Whh_.rows(), Whh_.cols());
    Matrix dWxh = Matrix::Zero(Wxh_.rows(), Wxh_.cols());
    Matrix dWhz = Matrix::Zero(Whz_.rows(), Whz_.cols());
    Matrix dWxz = Matrix::Zero(Wxz_.rows(), Wxz_.cols());
    Matrix dWhr = Matrix::Zero(Whr_.rows(), Whr_.cols());
    Matrix dWxr = Matrix::Zero(Wxr_.rows(), Wxr_.cols());
    Vector dby = Vector::Zero(Why_.rows());
    Vector dbh = Vector::Zero(Whh_.rows());
    Vector dbz = Vector::Zero(Whz_.rows());
    Vector dbr = Vector::Zero(Whr_.rows());
    Vector dhnext = Vector::Zero(Whh_.rows());

    Matrix dx = Matrix::Zero(GetInputSize(), input_sequence.size());

    for (int t = input_sequence.size() - 1; t >= 0; --t) {
      Vector dy = p_.col(t);
      dy[output_sequence[t]] -= 1.0; // Backprop into y (softmax grad).

      dWhy += dy * h_.col(t).transpose();
      dby += dy;

      Vector dh = Why_.transpose() * dy + dhnext; // Backprop into h.
      Vector dhu = z_.col(t).cwiseProduct(dh);
      Matrix dhuraw;
      DerivateActivation(activation_function_, hu_.col(t), &dhuraw);
      dhuraw = dhuraw.cwiseProduct(dhu);
      Vector hprev;
      if (t == 0) {
        hprev = Vector::Zero(h_.rows());
      } else {
        hprev = h_.col(t-1);
      }

      Vector dq = Whh_.transpose() * dhuraw;
      Vector dz = (hu_.col(t) - hprev).cwiseProduct(dh);
      Vector dr = hprev.cwiseProduct(dq);
      Matrix dzraw;
      DerivateActivation(ActivationFunctions::LOGISTIC, z_.col(t), &dzraw);
      dzraw = dzraw.cwiseProduct(dz);
      Matrix drraw;
      DerivateActivation(ActivationFunctions::LOGISTIC, r_.col(t), &drraw);
      drraw = drraw.cwiseProduct(dr);

      dWxz += dzraw * x_.col(t).transpose();
      dbz += dzraw;
      dWxr += drraw * x_.col(t).transpose();
      dbr += drraw;
      dWxh += dhuraw * x_.col(t).transpose();
      dbh += dhuraw;

      dWhz += dzraw * hprev.transpose();
      dWhr += drraw * hprev.transpose();
      dWhh += dhuraw * (r_.col(t).cwiseProduct(hprev)).transpose();

      dhnext.noalias() = Whz_.transpose() * dzraw + Whr_.transpose() * drraw +
        r_.col(t).cwiseProduct(dq) + (1.0 - z_.col(t).array()).matrix().cwiseProduct(dh);

      dx.col(t) = Wxz_.transpose() * dzraw + Wxr_.transpose() * drraw +
                  Wxh_.transpose() * dhuraw; // Backprop into x.
    }

    Why_ -= learning_rate * dWhy;
    by_ -= learning_rate * dby;
    Wxz_ -= learning_rate * dWxz;
    bz_ -= learning_rate * dbz;
    Wxr_ -= learning_rate * dWxr;
    br_ -= learning_rate * dbr;
    Wxh_ -= learning_rate * dWxh;
    bh_ -= learning_rate * dbh;
    Whz_ -= learning_rate * dWhz;
    Whr_ -= learning_rate * dWhr;
    Whh_ -= learning_rate * dWhh;
    if (use_hidden_start_) {
      h0_ -= learning_rate * dhnext;
    }

    RunBackwardLookupLayer(input_sequence, dx, learning_rate);
  }

 protected:
  Matrix Wxz_;
  Matrix Whz_;
  Matrix Wxr_;
  Matrix Whr_;
  Vector bz_;
  Vector br_;

  Matrix z_;
  Matrix r_;
  Matrix hu_;

};

class BiRNN_GRU : public RNN_GRU {
 public:
  BiRNN_GRU(Dictionary *dictionary,
            int window_size, int embedding_dimension,
            int affix_embedding_dimension,
            int hidden_size, int output_size) {
    dictionary_ = dictionary;
    activation_function_ = ActivationFunctions::LOGISTIC;
    window_size_ = window_size;
    embedding_dimension_ = embedding_dimension;
    affix_embedding_dimension_ = affix_embedding_dimension;
    hidden_size_ = hidden_size;
    output_size_ = output_size;
    use_hidden_start_ = true;
  }
  ~BiRNN_GRU() {}

  void CollectAllParameters(std::vector<Matrix*> *weights,
                            std::vector<Vector*> *biases,
                            std::vector<std::string> *weight_names,
                            std::vector<std::string> *bias_names) {
    RNN_GRU::CollectAllParameters(weights, biases, weight_names, bias_names);

    Wxz_r_ = Matrix::Zero(hidden_size_, GetInputSize());
    Wlz_r_ = Matrix::Zero(hidden_size_, hidden_size_);
    bz_r_ = Vector::Zero(hidden_size_, 1);

    Wxr_r_ = Matrix::Zero(hidden_size_, GetInputSize());
    Wlr_r_ = Matrix::Zero(hidden_size_, hidden_size_);
    br_r_ = Vector::Zero(hidden_size_, 1);

    Wxl_ = Matrix::Zero(hidden_size_, GetInputSize());
    Wll_ = Matrix::Zero(hidden_size_, hidden_size_);
    Wly_ = Matrix::Zero(output_size_, hidden_size_);
    bl_ = Vector::Zero(hidden_size_, 1);
    if (use_hidden_start_) {
      l0_ = Vector::Zero(hidden_size_);
    }

    weights->push_back(&Wxz_r_);
    weights->push_back(&Wlz_r_);
    weights->push_back(&Wxr_r_);
    weights->push_back(&Wlr_r_);

    weights->push_back(&Wxl_);
    weights->push_back(&Wll_);
    weights->push_back(&Wly_);

    biases->push_back(&bz_r_);
    biases->push_back(&br_r_);
    biases->push_back(&bl_);
    if (use_hidden_start_) {
      biases->push_back(&l0_); // Not really a bias, but it goes here.
    }

    weight_names->push_back("Wxz_r");
    weight_names->push_back("Wlz");
    weight_names->push_back("Wxr_r");
    weight_names->push_back("Wlr");

    weight_names->push_back("Wxl");
    weight_names->push_back("Wll");
    weight_names->push_back("Wly");

    bias_names->push_back("bz_r");
    bias_names->push_back("br_r");
    bias_names->push_back("bl");
    if (use_hidden_start_) {
      bias_names->push_back("l0");
    }
  }

  void RunForwardPass(const std::vector<Input> &input_sequence) {
    RunForwardLookupLayer(input_sequence);

    int hidden_size = Whh_.rows();
    int output_size = Why_.rows();

    z_.setZero(hidden_size, input_sequence.size());
    r_.setZero(hidden_size, input_sequence.size());
    hu_.setZero(hidden_size, input_sequence.size());
    h_.setZero(hidden_size, input_sequence.size());
    Vector hprev = Vector::Zero(h_.rows());
    if (use_hidden_start_) hprev = h0_;
    for (int t = 0; t < input_sequence.size(); ++t) {
      Matrix result;
      EvaluateActivation(ActivationFunctions::LOGISTIC,
                         Wxz_ * x_.col(t) + bz_ + Whz_ * hprev,
                         &result);
      z_.col(t) = result;

      EvaluateActivation(ActivationFunctions::LOGISTIC,
                         Wxr_ * x_.col(t) + br_ + Whr_ * hprev,
                         &result);
      r_.col(t) = result;

      EvaluateActivation(activation_function_,
                         Wxh_ * x_.col(t) + bh_ + Whh_ * r_.col(t).cwiseProduct(hprev),
                         &result);
      hu_.col(t) = result;

      //h_.col(t) = z_.col(t) * hu_.col(t) + (1.0 - z_.col(t)) * hprev;
      h_.col(t) = z_.col(t).cwiseProduct(hu_.col(t) - hprev) + hprev;

      //Vector hraw = Wxh_ * x_.col(t) + bh_;
      //if (t > 0) hraw += Whh_ * h_.col(t-1);
      //Matrix result;
      //EvaluateActivation(activation_function_, hraw, &result);
      //h_.col(t) = result;

      hprev = h_.col(t);
    }

    z_r_.setZero(hidden_size, input_sequence.size());
    r_r_.setZero(hidden_size, input_sequence.size());
    lu_.setZero(hidden_size, input_sequence.size());
    l_.setZero(hidden_size, input_sequence.size());
    Vector lnext = Vector::Zero(l_.rows());
    if (use_hidden_start_) lnext = l0_;
    for (int t = input_sequence.size() - 1; t >= 0; --t) {
      Matrix result;
      EvaluateActivation(ActivationFunctions::LOGISTIC,
                         Wxz_r_ * x_.col(t) + bz_r_ + Wlz_r_ * lnext,
                         &result);
      z_r_.col(t) = result;

      EvaluateActivation(ActivationFunctions::LOGISTIC,
                         Wxr_r_ * x_.col(t) + br_r_ + Wlr_r_ * lnext,
                         &result);
      r_r_.col(t) = result;

      EvaluateActivation(activation_function_,
                         Wxl_ * x_.col(t) + bl_ + Wll_ * r_r_.col(t).cwiseProduct(lnext),
                         &result);
      lu_.col(t) = result;

      l_.col(t) = z_r_.col(t).cwiseProduct(lu_.col(t) - lnext) + lnext;
      lnext = l_.col(t);
    }

    y_.setZero(output_size, input_sequence.size());
    p_.setZero(output_size, input_sequence.size());
    for (int t = 0; t < input_sequence.size(); ++t) {
      y_.col(t) = Why_ * h_.col(t) +  Wly_ * l_.col(t) + by_;
      double logsum = LogSumExp(y_.col(t));
      p_.col(t) = (y_.col(t).array() - logsum).exp();
    }
  }

  void RunBackwardPass(const std::vector<Input> &input_sequence,
                       const std::vector<int> &output_sequence,
                       double learning_rate) {
    Matrix dWhy = Matrix::Zero(Why_.rows(), Why_.cols());
    Matrix dWhh = Matrix::Zero(Whh_.rows(), Whh_.cols());
    Matrix dWxh = Matrix::Zero(Wxh_.rows(), Wxh_.cols());
    Matrix dWhz = Matrix::Zero(Whz_.rows(), Whz_.cols());
    Matrix dWxz = Matrix::Zero(Wxz_.rows(), Wxz_.cols());
    Matrix dWhr = Matrix::Zero(Whr_.rows(), Whr_.cols());
    Matrix dWxr = Matrix::Zero(Wxr_.rows(), Wxr_.cols());
    Vector dby = Vector::Zero(Why_.rows());
    Vector dbh = Vector::Zero(Whh_.rows());
    Vector dbz = Vector::Zero(Whz_.rows());
    Vector dbr = Vector::Zero(Whr_.rows());
    Vector dhnext = Vector::Zero(Whh_.rows());

    Matrix dWly = Matrix::Zero(Wly_.rows(), Wly_.cols());
    Matrix dWll = Matrix::Zero(Wll_.rows(), Wll_.cols());
    Matrix dWxl = Matrix::Zero(Wxl_.rows(), Wxl_.cols());
    Matrix dWlz_r = Matrix::Zero(Wlz_r_.rows(), Wlz_r_.cols());
    Matrix dWxz_r = Matrix::Zero(Wxz_r_.rows(), Wxz_r_.cols());
    Matrix dWlr_r = Matrix::Zero(Wlr_r_.rows(), Wlr_r_.cols());
    Matrix dWxr_r = Matrix::Zero(Wxr_r_.rows(), Wxr_r_.cols());
    Vector dbl = Vector::Zero(Wll_.rows());
    Vector dbz_r = Vector::Zero(Wlz_r_.rows());
    Vector dbr_r = Vector::Zero(Wlr_r_.rows());
    Vector dlprev = Vector::Zero(Wll_.rows());

    Matrix dx = Matrix::Zero(GetInputSize(), input_sequence.size());

    // TODO(atm): here.

    for (int t = 0; t < input_sequence.size(); ++t) {
      Vector dy = p_.col(t);
      dy[output_sequence[t]] -= 1.0; // Backprop into y (softmax grad).

      dWly += dy * l_.col(t).transpose();

      Vector dl = Wly_.transpose() * dy + dlprev; // Backprop into l.
      Vector dlu = z_r_.col(t).cwiseProduct(dl);
      Matrix dluraw;
      DerivateActivation(activation_function_, lu_.col(t), &dluraw);
      dluraw = dluraw.cwiseProduct(dlu);
      Vector lnext;
      if (t == input_sequence.size() - 1) {
        lnext = Vector::Zero(l_.rows());
      } else {
        lnext = l_.col(t+1);
      }

      Vector dq_r = Wll_.transpose() * dluraw;
      Vector dz_r = (lu_.col(t) - lnext).cwiseProduct(dl);
      Vector dr_r = lnext.cwiseProduct(dq_r);
      Matrix dzraw_r;
      DerivateActivation(ActivationFunctions::LOGISTIC, z_r_.col(t), &dzraw_r);
      dzraw_r = dzraw_r.cwiseProduct(dz_r);
      Matrix drraw_r;
      DerivateActivation(ActivationFunctions::LOGISTIC, r_r_.col(t), &drraw_r);
      drraw_r = drraw_r.cwiseProduct(dr_r);

      dWxz_r += dzraw_r * x_.col(t).transpose();
      dbz_r += dzraw_r;
      dWxr_r += drraw_r * x_.col(t).transpose();
      dbr_r += drraw_r;
      dWxl += dluraw * x_.col(t).transpose();
      dbl += dluraw;

      dWlz_r += dzraw_r * lnext.transpose();
      dWlr_r += drraw_r * lnext.transpose();
      dWll += dluraw * (r_r_.col(t).cwiseProduct(lnext)).transpose();

      dlprev.noalias() = Wlz_r_.transpose() * dzraw_r + Wlr_r_.transpose() * drraw_r +
        r_r_.col(t).cwiseProduct(dq_r) + (1.0 - z_r_.col(t).array()).matrix().cwiseProduct(dl);

      dx.col(t) = Wxz_r_.transpose() * dzraw_r + Wxr_r_.transpose() * drraw_r +
                  Wxl_.transpose() * dluraw; // Backprop into x.
    }

    for (int t = input_sequence.size() - 1; t >= 0; --t) {
      Vector dy = p_.col(t);
      dy[output_sequence[t]] -= 1.0; // Backprop into y (softmax grad).

      dWhy += dy * h_.col(t).transpose();
      dby += dy;

      Vector dh = Why_.transpose() * dy + dhnext; // Backprop into h.
      Vector dhu = z_.col(t).cwiseProduct(dh);
      Matrix dhuraw;
      DerivateActivation(activation_function_, hu_.col(t), &dhuraw);
      dhuraw = dhuraw.cwiseProduct(dhu);
      Vector hprev;
      if (t == 0) {
        hprev = Vector::Zero(h_.rows());
      } else {
        hprev = h_.col(t-1);
      }

      Vector dq = Whh_.transpose() * dhuraw;
      Vector dz = (hu_.col(t) - hprev).cwiseProduct(dh);
      Vector dr = hprev.cwiseProduct(dq);
      Matrix dzraw;
      DerivateActivation(ActivationFunctions::LOGISTIC, z_.col(t), &dzraw);
      dzraw = dzraw.cwiseProduct(dz);
      Matrix drraw;
      DerivateActivation(ActivationFunctions::LOGISTIC, r_.col(t), &drraw);
      drraw = drraw.cwiseProduct(dr);

      dWxz += dzraw * x_.col(t).transpose();
      dbz += dzraw;
      dWxr += drraw * x_.col(t).transpose();
      dbr += drraw;
      dWxh += dhuraw * x_.col(t).transpose();
      dbh += dhuraw;

      dWhz += dzraw * hprev.transpose();
      dWhr += drraw * hprev.transpose();
      dWhh += dhuraw * (r_.col(t).cwiseProduct(hprev)).transpose();

      dhnext.noalias() = Whz_.transpose() * dzraw + Whr_.transpose() * drraw +
        r_.col(t).cwiseProduct(dq) + (1.0 - z_.col(t).array()).matrix().cwiseProduct(dh);

      dx.col(t) += Wxz_.transpose() * dzraw + Wxr_.transpose() * drraw +
                   Wxh_.transpose() * dhuraw; // Backprop into x.
    }

    Why_ -= learning_rate * dWhy;
    by_ -= learning_rate * dby;
    Wxz_ -= learning_rate * dWxz;
    bz_ -= learning_rate * dbz;
    Wxr_ -= learning_rate * dWxr;
    br_ -= learning_rate * dbr;
    Wxh_ -= learning_rate * dWxh;
    bh_ -= learning_rate * dbh;
    Whz_ -= learning_rate * dWhz;
    Whr_ -= learning_rate * dWhr;
    Whh_ -= learning_rate * dWhh;
    if (use_hidden_start_) {
      h0_ -= learning_rate * dhnext;
    }

    Wly_ -= learning_rate * dWly;
    Wxz_r_ -= learning_rate * dWxz_r;
    bz_r_ -= learning_rate * dbz_r;
    Wxr_r_ -= learning_rate * dWxr_r;
    br_r_ -= learning_rate * dbr_r;
    Wxl_ -= learning_rate * dWxl;
    bl_ -= learning_rate * dbl;
    Wlz_r_ -= learning_rate * dWlz_r;
    Wlr_r_ -= learning_rate * dWlr_r;
    Wll_ -= learning_rate * dWll;
    if (use_hidden_start_) {
      l0_ -= learning_rate * dlprev;
    }

    RunBackwardLookupLayer(input_sequence, dx, learning_rate);
  }

 protected:
  Matrix Wxl_;
  Matrix Wll_;
  Matrix Wly_;
  Matrix Wxz_r_;
  Matrix Wlz_r_;
  Matrix Wxr_r_;
  Matrix Wlr_r_;
  Vector bl_;
  Vector bz_r_;
  Vector br_r_;
  Vector l0_;

  Matrix l_;
  Matrix z_r_;
  Matrix r_r_;
  Matrix lu_;
};

#endif

#endif /* RNN_H_ */
